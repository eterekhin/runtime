// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

#include "common.h"
#include "gcenv.h"
#include <exinfo.h>

#if defined(FEATURE_EH_FUNCLETS)
#if defined(USE_GC_INFO_DECODER)

struct FindFirstInterruptiblePointState
{
    unsigned offs;
    unsigned endOffs;
    unsigned returnOffs;
};

bool FindFirstInterruptiblePointStateCB(
        UINT32 startOffset,
        UINT32 stopOffset,
        LPVOID hCallback)
{
    FindFirstInterruptiblePointState* pState = (FindFirstInterruptiblePointState*)hCallback;

    _ASSERTE(startOffset < stopOffset);
    _ASSERTE(pState->offs < pState->endOffs);

    if (stopOffset <= pState->offs)
    {
        // The range ends before the requested offset.
        return false;
    }

    // The offset is in the range.
    if (startOffset <= pState->offs &&
                       pState->offs < stopOffset)
    {
        pState->returnOffs = pState->offs;
        return true;
    }

    // The range is completely after the desired offset. We use the range start offset, if
    // it comes before the given endOffs. We assume that the callback is called with ranges
    // in increasing order, so earlier ones are reported before later ones. That is, if we
    // get to this case, it will be the closest interruptible range after the requested
    // offset.

    _ASSERTE(pState->offs < startOffset);
    if (startOffset < pState->endOffs)
    {
        pState->returnOffs = startOffset;
        return true;
    }

    return false;
}

// Find the first interruptible point in the range [offs .. endOffs) (the beginning of the range is inclusive,
// the end is exclusive). Return -1 if no such point exists.
unsigned FindFirstInterruptiblePoint(CrawlFrame* pCF, unsigned offs, unsigned endOffs)
{
    GCInfoToken gcInfoToken = pCF->GetGCInfoToken();
    GcInfoDecoder gcInfoDecoder(gcInfoToken, DECODE_FOR_RANGES_CALLBACK);

    FindFirstInterruptiblePointState state;
    state.offs = offs;
    state.endOffs = endOffs;
    state.returnOffs = -1;

    gcInfoDecoder.EnumerateInterruptibleRanges(&FindFirstInterruptiblePointStateCB, &state);

    return state.returnOffs;
}
#else // USE_GC_INFO_DECODER

// Find the first interruptible point in the range [offs .. endOffs) (the beginning of the range is inclusive,
// the end is exclusive). Return -1 if no such point exists.
unsigned FindFirstInterruptiblePoint(CrawlFrame* pCF, unsigned offs, unsigned endOffs)
{
    EECodeInfo *codeInfo = pCF->GetCodeInfo();
    hdrInfo info = { 0 };
    PTR_CBYTE table = codeInfo->DecodeGCHdrInfo(&info, offs);
    // NOTE: We make an assumption that we are not searching for the main function so we can ignore the
    // prolog/epilog info.
    return ::FindFirstInterruptiblePoint(&info, table, offs, endOffs);
}

#endif
#endif // FEATURE_EH_FUNCLETS

//-----------------------------------------------------------------------------
// Determine whether we should report the generic parameter context
//
// This is meant to detect following situations:
//
// When a ThreadAbortException is raised
// in the prolog of a managed method, before the location for the generics
// context has been initialized; when such a TAE is raised, we are open to a
// race with the GC (e.g. while creating the managed object for the TAE).
// The GC would cause a stack walk, and if we report the stack location for
// the generic param context at this time we'd crash.
// The long term solution is to avoid raising TAEs in any non-GC safe points,
// and to additionally ensure that we do not expose the runtime to TAE
// starvation.
//
// When we're in the process of resolution of an interface method and the
// interface method happens to have a default implementation. Normally,
// such methods require a generic context, but since we didn't resolve the
// method to an implementation yet, we don't have the right context (in fact,
// there's no context provided by the caller).
// See code:getMethodSigInternal
//
inline bool SafeToReportGenericParamContext(CrawlFrame* pCF)
{
    LIMITED_METHOD_CONTRACT;

    if (!pCF->IsFrameless() && pCF->GetFrame()->GetFrameIdentifier() == FrameIdentifier::StubDispatchFrame)
    {
        return !(dac_cast<PTR_StubDispatchFrame>(pCF->GetFrame()))->SuppressParamTypeArg();
    }

    if (!pCF->IsFrameless() || !(pCF->IsActiveFrame() || pCF->IsInterrupted()))
    {
        return true;
    }

#ifndef USE_GC_INFO_DECODER

    ICodeManager * pEECM = pCF->GetCodeManager();
    if (pEECM != NULL && pEECM->IsInPrologOrEpilog(pCF->GetRelOffset(), pCF->GetGCInfoToken(), NULL))
    {
        return false;
    }

#else  // USE_GC_INFO_DECODER

    GcInfoDecoder gcInfoDecoder(pCF->GetGCInfoToken(),
            DECODE_PROLOG_LENGTH);
    UINT32 prologLength = gcInfoDecoder.GetPrologSize();
    if (pCF->GetRelOffset() < prologLength)
    {
        return false;
    }

#endif // USE_GC_INFO_DECODER

    return true;
}

/*
 * CheckDoubleReporting()
 *
 * This function is used to check for double reporting of the same stack slot or register.
 * Double reporting is not allowed unless the reference is pinned, since it would
 * result in incorrect updating of a reference in the slot in case the GC moves the
 * object.
 */
void CheckDoubleReporting(GCCONTEXT *pCtx, Object **ppObj, uint32_t flags)
{
    if (((flags & GC_CALL_PINNED) == 0) && pCtx->sc->promotion)
    {
        if (pCtx->pScannedSlots == NULL)
        {
            pCtx->pScannedSlots = new (nothrow) SetSHash<Object**, PtrSetSHashTraits<Object**> >();
            if (pCtx->pScannedSlots == NULL)
            {
                return;
            }
        }
        _ASSERTE_ALL_BUILDS(pCtx->pScannedSlots->Lookup(ppObj) == NULL);
        pCtx->pScannedSlots->AddNoThrow(ppObj);
    }
}

/*
 * GcEnumObject()
 *
 * This is the JIT compiler (or any remote code manager)
 * GC enumeration callback
 */

void GcEnumObject(LPVOID pData, OBJECTREF *pObj, uint32_t flags)
{
    Object ** ppObj = (Object **)pObj;
    GCCONTEXT   * pCtx  = (GCCONTEXT *) pData;

    if (g_pConfig->GetCheckDoubleReporting())
    {
        CheckDoubleReporting(pCtx, ppObj, flags);
    }

    // Since we may be asynchronously walking another thread's stack,
    // check (frequently) for stack-buffer-overrun corruptions after
    // any long operation
    if (pCtx->cf != NULL)
        pCtx->cf->CheckGSCookies();

    //
    // Sanity check that the flags contain only these three values
    //
    assert((flags & ~(GC_CALL_INTERIOR|GC_CALL_PINNED)) == 0);

    // for interior pointers, we optimize the case in which
    //  it points into the current threads stack area
    //
    if (flags & GC_CALL_INTERIOR)
        PromoteCarefully(pCtx->f, ppObj, pCtx->sc, flags);
    else
        (pCtx->f)(ppObj, pCtx->sc, flags);
}

//-----------------------------------------------------------------------------
void GcReportLoaderAllocator(promote_func* fn, ScanContext* sc, LoaderAllocator *pLoaderAllocator)
{
    CONTRACTL
    {
        NOTHROW;
        GC_NOTRIGGER;
        MODE_COOPERATIVE;
    }
    CONTRACTL_END;

    if (pLoaderAllocator != NULL && pLoaderAllocator->IsCollectible())
    {
        Object *refCollectionObject = OBJECTREFToObject(pLoaderAllocator->GetExposedObject());
        if (refCollectionObject != NULL)
        {
            INDEBUG(Object *oldObj = refCollectionObject;)
            fn(&refCollectionObject, sc, CHECK_APP_DOMAIN);

            // We are reporting the location of a local variable, assert it doesn't change.
            _ASSERTE(oldObj == refCollectionObject);
        }
    }
}

//-----------------------------------------------------------------------------
StackWalkAction GcStackCrawlCallBack(CrawlFrame* pCF, VOID* pData)
{
    //
    // KEEP IN SYNC WITH DacStackReferenceWalker::Callback in debug\daccess\daccess.cpp
    //

    GCCONTEXT   *gcctx = (GCCONTEXT*) pData;

    MethodDesc *pMD = pCF->GetFunction();

#ifdef GC_PROFILING
    gcctx->sc->pMD = pMD;
#endif //GC_PROFILING

    // Clear it on exit so that we never have a stale CrawlFrame
    ResetPointerHolder<CrawlFrame*> rph(&gcctx->cf);
    // put it somewhere so that GcEnumObject can get to it.
    gcctx->cf = pCF;

    bool fReportGCReferences = true;
#if defined(FEATURE_EH_FUNCLETS)
    // We may have unwound this crawlFrame and thus, shouldn't report the invalid
    // references it may contain.
    fReportGCReferences = pCF->ShouldCrawlframeReportGCReferences();

    Thread *pThread = pCF->GetThread();
    ExInfo *pExInfo = (ExInfo *)pThread->GetExceptionState()->GetCurrentExceptionTracker();

    if (pCF->ShouldSaveFuncletInfo())
    {
        STRESS_LOG3(LF_GCROOTS, LL_INFO1000, "Saving info on funclet at SP: %p, PC: %p, FP: %p\n",
            GetRegdisplaySP(pCF->GetRegisterSet()), GetControlPC(pCF->GetRegisterSet()), GetFP(pCF->GetRegisterSet()->pCurrentContext));

        _ASSERTE(pExInfo);
        REGDISPLAY *pRD = pCF->GetRegisterSet();
        pExInfo->m_lastReportedFunclet.IP = GetControlPC(pRD);
        pExInfo->m_lastReportedFunclet.FP = GetFP(pRD->pCurrentContext);
        pExInfo->m_lastReportedFunclet.Flags = pCF->GetCodeManagerFlags();
    }

    if (pCF->ShouldParentToFuncletReportSavedFuncletSlots())
    {
        STRESS_LOG4(LF_GCROOTS, LL_INFO1000, "Reporting slots in funclet parent frame method at SP: %p, PC: %p using original FP: %p, PC: %p\n",
            GetRegdisplaySP(pCF->GetRegisterSet()), GetControlPC(pCF->GetRegisterSet()), pExInfo->m_lastReportedFunclet.FP, pExInfo->m_lastReportedFunclet.IP);

        _ASSERTE(!pCF->ShouldParentToFuncletUseUnwindTargetLocationForGCReporting());
        _ASSERTE(pExInfo);

        ICodeManager * pCM = pCF->GetCodeManager();
        _ASSERTE(pCM != NULL);

        CONTEXT context = {};
        REGDISPLAY partialRD;
        SetIP(&context, pExInfo->m_lastReportedFunclet.IP);
        SetFP(&context, pExInfo->m_lastReportedFunclet.FP);
        SetSP(&context, 0);

        context.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
        FillRegDisplay(&partialRD, &context);

        EECodeInfo codeInfo(pExInfo->m_lastReportedFunclet.IP);
        _ASSERTE(codeInfo.IsValid());

        pCM->EnumGcRefs(&partialRD,
                        &codeInfo,
                        pExInfo->m_lastReportedFunclet.Flags | ReportFPBasedSlotsOnly,
                        GcEnumObject,
                        pData,
                        NO_OVERRIDE_OFFSET);
    }
    else
#endif // defined(FEATURE_EH_FUNCLETS)
    if (fReportGCReferences)
    {
        if (pCF->IsFrameless())
        {
            ICodeManager * pCM = pCF->GetCodeManager();
            _ASSERTE(pCM != NULL);

            unsigned flags = pCF->GetCodeManagerFlags();

    #ifdef TARGET_X86
            STRESS_LOG3(LF_GCROOTS, LL_INFO1000, "Scanning Frameless method %pM EIP = %p &EIP = %p\n",
                pMD, GetControlPC(pCF->GetRegisterSet()), GetRegdisplayPCTAddr(pCF->GetRegisterSet()));
    #else
            STRESS_LOG2(LF_GCROOTS, LL_INFO1000, "Scanning Frameless method %pM ControlPC = %p\n",
                pMD, GetControlPC(pCF->GetRegisterSet()));
    #endif

            _ASSERTE(pMD != 0);

    #ifdef _DEBUG
            LOG((LF_GCROOTS, LL_INFO1000, "Scanning Frame for method %s:%s\n",
                    pMD->m_pszDebugClassName, pMD->m_pszDebugMethodName));
    #endif // _DEBUG

            DWORD relOffsetOverride = NO_OVERRIDE_OFFSET;
#if defined(FEATURE_EH_FUNCLETS)
            if (pCF->ShouldParentToFuncletUseUnwindTargetLocationForGCReporting())
            {
                // We're in a special case of unwinding from a funclet, and resuming execution in
                // another catch funclet associated with same parent function. We need to report roots.
                // Reporting at the original throw site gives incorrect liveness information. We choose to
                // report the liveness information at the first interruptible instruction of the catch funclet
                // that we are going to execute. We also only report stack slots, since no registers can be
                // live at the first instruction of a handler, except the catch object, which the VM protects
                // specially. If the catch funclet has not interruptible point, we fall back and just report
                // what we used to: at the original throw instruction. This might lead to bad GC behavior
                // if the liveness is not correct.
                const EE_ILEXCEPTION_CLAUSE& ehClauseForCatch = pCF->GetEHClauseForCatch();
                relOffsetOverride = FindFirstInterruptiblePoint(pCF, ehClauseForCatch.HandlerStartPC,
                                                                ehClauseForCatch.HandlerEndPC);
                _ASSERTE(relOffsetOverride != NO_OVERRIDE_OFFSET);

                STRESS_LOG3(LF_GCROOTS, LL_INFO1000, "Setting override offset = %u for method %pM ControlPC = %p\n",
                    relOffsetOverride, pMD, GetControlPC(pCF->GetRegisterSet()));
            }
#endif // FEATURE_EH_FUNCLETS

            pCM->EnumGcRefs(pCF->GetRegisterSet(),
                            pCF->GetCodeInfo(),
                            flags,
                            GcEnumObject,
                            pData,
                            relOffsetOverride);

        }
        else
        {
            Frame * pFrame = pCF->GetFrame();

            STRESS_LOG3(LF_GCROOTS, LL_INFO1000,
                "Scanning ExplicitFrame %p AssocMethod = %pM FrameIdentifier = %s\n",
                pFrame, pFrame->GetFunction(), Frame::GetFrameTypeName(pFrame->GetFrameIdentifier()));
            pFrame->GcScanRoots( gcctx->f, gcctx->sc);
        }
    }
    else
    {
        STRESS_LOG2(LF_GCROOTS, LL_INFO1000, "Skipping GC scanning in frame method at SP: %p, PC: %p\n",
            GetRegdisplaySP(pCF->GetRegisterSet()), GetControlPC(pCF->GetRegisterSet()));
    }

    // If we're executing a LCG dynamic method then we must promote the associated resolver to ensure it
    // doesn't get collected and yank the method code out from under us).

    // Be careful to only promote the reference -- we can also be called to relocate the reference and
    // that can lead to all sorts of problems since we could be racing for the relocation with the long
    // weak handle we recover the reference from. Promoting the reference is enough, the handle in the
    // reference will be relocated properly as long as we keep it alive till the end of the collection
    // as long as the reference is actually maintained by the long weak handle.
    if (pMD && gcctx->sc->promotion)
    {
        BOOL fMaybeCollectibleMethod = TRUE;

        // If this is a frameless method then the jitmanager can answer the question of whether
        // or not this is LCG simply by looking at the heap where the code lives, however there
        // is also the prestub case where we need to explicitly look at the MD for stuff that isn't
        // ngen'd
        if (pCF->IsFrameless())
        {
            fMaybeCollectibleMethod = ExecutionManager::IsCollectibleMethod(pCF->GetMethodToken());
        }

        if (fMaybeCollectibleMethod && pMD->IsLCGMethod())
        {
            Object *refResolver = OBJECTREFToObject(pMD->AsDynamicMethodDesc()->GetLCGMethodResolver()->GetManagedResolver());
#ifdef _DEBUG
            Object *oldObj = refResolver;
#endif
            _ASSERTE(refResolver != NULL);
            (*gcctx->f)(&refResolver, gcctx->sc, CHECK_APP_DOMAIN);
            _ASSERTE(!pMD->IsSharedByGenericInstantiations());

            // We are reporting the location of a local variable, assert it doesn't change.
            _ASSERTE(oldObj == refResolver);
        }
        else
        {
            if (fMaybeCollectibleMethod)
            {
                GcReportLoaderAllocator(gcctx->f, gcctx->sc, pMD->GetLoaderAllocator());
            }

            if (fReportGCReferences)
            {
                GenericParamContextType paramContextType = GENERIC_PARAM_CONTEXT_NONE;

                if (pCF->IsFrameless())
                {
                    // We need to grab the Context Type here because there are cases where the MethodDesc
                    // is shared, and thus indicates there should be an instantion argument, but the JIT
                    // was still allowed to optimize it away and we won't grab it below because we're not
                    // reporting any references from this frame.
                    paramContextType = pCF->GetCodeManager()->GetParamContextType(pCF->GetRegisterSet(), pCF->GetCodeInfo());
                }
                else
                {
                    if (pMD->RequiresInstMethodDescArg())
                        paramContextType = GENERIC_PARAM_CONTEXT_METHODDESC;
                    else if (pMD->RequiresInstMethodTableArg())
                        paramContextType = GENERIC_PARAM_CONTEXT_METHODTABLE;
                }

                if (paramContextType != GENERIC_PARAM_CONTEXT_NONE && SafeToReportGenericParamContext(pCF))
                {
                    // Handle the case where the method is a static shared generic method and we need to keep the type
                    // of the generic parameters alive
                    if (paramContextType == GENERIC_PARAM_CONTEXT_METHODDESC)
                    {
                        MethodDesc *pMDReal = dac_cast<PTR_MethodDesc>(pCF->GetParamTypeArg());
                        // Async methods may be in a state when the context is not yet restored from continuation.
                        // We will allow that. The context is reachable via continuation in such case.
                        _ASSERTE((pMDReal != NULL) || !pCF->IsFrameless() || pMD->IsAsyncMethod());
                        if (pMDReal != NULL)
                        {
                            GcReportLoaderAllocator(gcctx->f, gcctx->sc, pMDReal->GetLoaderAllocator());
                        }
                    }
                    else if (paramContextType == GENERIC_PARAM_CONTEXT_METHODTABLE)
                    {
                        MethodTable *pMTReal = dac_cast<PTR_MethodTable>(pCF->GetParamTypeArg());
                        // Async methods may be in a state when the context is not yet restored from continuation.
                        // We will allow that. The context is reachable via continuation in such case.
                        _ASSERTE((pMTReal != NULL) || !pCF->IsFrameless() || pMD->IsAsyncMethod());
                        if (pMTReal != NULL)
                        {
                            GcReportLoaderAllocator(gcctx->f, gcctx->sc, pMTReal->GetLoaderAllocator());
                        }
                    }
                }
            }
        }
    }

    // Since we may be asynchronously walking another thread's stack,
    // check (frequently) for stack-buffer-overrun corruptions after
    // any long operation
    pCF->CheckGSCookies();

    return SWA_CONTINUE;
}
