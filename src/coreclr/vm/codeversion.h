// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.
// ===========================================================================
// File: CodeVersion.h
//
// ===========================================================================


#ifndef CODE_VERSION_H
#define CODE_VERSION_H

class ILCodeVersion;
typedef DWORD NativeCodeVersionId;

#ifdef FEATURE_CODE_VERSIONING
class NativeCodeVersionNode;
typedef DPTR(class NativeCodeVersionNode) PTR_NativeCodeVersionNode;
class NativeCodeVersionCollection;
class NativeCodeVersionIterator;
class ILCodeVersionNode;
typedef DPTR(class ILCodeVersionNode) PTR_ILCodeVersionNode;
class ILCodeVersionCollection;
class ILCodeVersionIterator;
class MethodDescVersioningState;
typedef DPTR(class MethodDescVersioningState) PTR_MethodDescVersioningState;

class ILCodeVersioningState;
typedef DPTR(class ILCodeVersioningState) PTR_ILCodeVersioningState;
class CodeVersionManager;
typedef DPTR(class CodeVersionManager) PTR_CodeVersionManager;

#endif

#ifdef HAVE_GCCOVER
class GCCoverageInfo;
typedef DPTR(class GCCoverageInfo) PTR_GCCoverageInfo;
#endif

#ifdef FEATURE_ON_STACK_REPLACEMENT
struct PatchpointInfo;
typedef DPTR(struct PatchpointInfo) PTR_PatchpointInfo;
#endif

class NativeCodeVersion
{
#ifdef FEATURE_CODE_VERSIONING
    friend class MethodDescVersioningState;
    friend class ILCodeVersion;
#endif

public:
    NativeCodeVersion();
    NativeCodeVersion(const NativeCodeVersion & rhs);
#ifdef FEATURE_CODE_VERSIONING
    NativeCodeVersion(PTR_NativeCodeVersionNode pVersionNode);
#endif
    explicit NativeCodeVersion(PTR_MethodDesc pMethod);

    BOOL IsNull() const;
    PTR_MethodDesc GetMethodDesc() const;
    NativeCodeVersionId GetVersionId() const;
    BOOL IsDefaultVersion() const;
    PCODE GetNativeCode() const;

#ifdef FEATURE_CODE_VERSIONING
    ILCodeVersion GetILCodeVersion() const;
    ReJITID GetILCodeVersionId() const;
#endif

#ifndef DACCESS_COMPILE
    BOOL SetNativeCodeInterlocked(PCODE pCode, PCODE pExpected = 0);
#endif

    // NOTE: Don't change existing values to avoid breaking changes in event tracing
    enum OptimizationTier
    {
        OptimizationTier0,
        OptimizationTier1,
        OptimizationTier1OSR,
        OptimizationTierOptimized, // may do less optimizations than tier 1
        OptimizationTier0Instrumented,
        OptimizationTier1Instrumented,
    };
#ifdef FEATURE_TIERED_COMPILATION
    OptimizationTier GetOptimizationTier() const;
    bool IsFinalTier() const;
#ifndef DACCESS_COMPILE
    void SetOptimizationTier(OptimizationTier tier);
#endif
#endif // FEATURE_TIERED_COMPILATION

#ifdef FEATURE_ON_STACK_REPLACEMENT
    PatchpointInfo * GetOSRInfo(unsigned * iloffset);
#endif // FEATURE_ON_STACK_REPLACEMENT

#ifdef HAVE_GCCOVER
    PTR_GCCoverageInfo GetGCCoverageInfo() const;
    void SetGCCoverageInfo(PTR_GCCoverageInfo gcCover);
#endif

    bool operator==(const NativeCodeVersion & rhs) const;
    bool operator!=(const NativeCodeVersion & rhs) const;

#if defined(DACCESS_COMPILE) && defined(FEATURE_CODE_VERSIONING)
    // The DAC is privy to the backing node abstraction
    PTR_NativeCodeVersionNode AsNode() const;
#endif

private:

#ifndef FEATURE_CODE_VERSIONING
    PTR_MethodDesc m_pMethodDesc;
#else // FEATURE_CODE_VERSIONING

#ifndef DACCESS_COMPILE
    NativeCodeVersionNode* AsNode() const;
    NativeCodeVersionNode* AsNode();
    void SetActiveChildFlag(BOOL isActive);
    MethodDescVersioningState* GetMethodDescVersioningState();
#endif

    BOOL IsActiveChildVersion() const;
    PTR_MethodDescVersioningState GetMethodDescVersioningState() const;

    enum StorageKind
    {
        Unknown,
        Explicit,
        Synthetic
    };

    StorageKind m_storageKind;
    union
    {
        PTR_NativeCodeVersionNode m_pVersionNode;
        struct
        {
            PTR_MethodDesc m_pMethodDesc;
        } m_synthetic;
    };
#endif // FEATURE_CODE_VERSIONING
};



#ifdef FEATURE_CODE_VERSIONING

enum class RejitFlags : uint32_t
{
    // The profiler has requested a ReJit, so we've allocated stuff, but we haven't
    // called back to the profiler to get any info or indicate that the ReJit has
    // started. (This Info can be 'reused' for a new ReJit if the
    // profiler calls RequestRejit again before we transition to the next state.)
    kStateRequested = 0x00000000,

    // The CLR has initiated the call to the profiler's GetReJITParameters() callback
    // but it hasn't completed yet. At this point we have to assume the profiler has
    // committed to a specific IL body, even if the CLR doesn't know what it is yet.
    // If the profiler calls RequestRejit we need to allocate a new ILCodeVersion
    // and call GetReJITParameters() again.
    kStateGettingReJITParameters = 0x00000001,

    // We have asked the profiler about this method via ICorProfilerFunctionControl,
    // and have thus stored the IL and codegen flags the profiler specified.
    kStateActive = 0x00000002,

    kStateMask = 0x0000000F,

    // Indicates that the method being ReJITted is an inliner of the actual
    // ReJIT request and we should not issue the GetReJITParameters for this
    // method.
    kSuppressParams = 0x80000000,

    support_use_as_flags // Enable the template functions in enum_class_flags.h
};

class ILCodeVersion
{
    friend class NativeCodeVersionIterator;

public:
    ILCodeVersion();
    ILCodeVersion(const ILCodeVersion & ilCodeVersion);
    ILCodeVersion(PTR_ILCodeVersionNode pILCodeVersionNode);
    ILCodeVersion(PTR_Module pModule, mdMethodDef methodDef);

    bool operator==(const ILCodeVersion & rhs) const;
    bool operator!=(const ILCodeVersion & rhs) const;
    BOOL HasDefaultIL() const;
    BOOL IsNull() const;
    BOOL IsDefaultVersion() const;
    PTR_Module GetModule() const;
    mdMethodDef GetMethodDef() const;
    ReJITID GetVersionId() const;
    NativeCodeVersionCollection GetNativeCodeVersions(PTR_MethodDesc pClosedMethodDesc) const;
    NativeCodeVersion GetActiveNativeCodeVersion(PTR_MethodDesc pClosedMethodDesc) const;
#if defined(FEATURE_TIERED_COMPILATION) && !defined(DACCESS_COMPILE)
    bool HasAnyOptimizedNativeCodeVersion(NativeCodeVersion tier0NativeCodeVersion) const;
#endif
    PTR_COR_ILMETHOD GetIL() const;
    DWORD GetJitFlags() const;
    const InstrumentedILOffsetMapping* GetInstrumentedILMap() const;

#ifndef DACCESS_COMPILE
    void SetIL(COR_ILMETHOD* pIL);
    void SetJitFlags(DWORD flags);
    void SetInstrumentedILMap(SIZE_T cMap, COR_IL_MAP * rgMap);
    HRESULT AddNativeCodeVersion(MethodDesc* pClosedMethodDesc, NativeCodeVersion::OptimizationTier optimizationTier,
        NativeCodeVersion* pNativeCodeVersion, PatchpointInfo* patchpointInfo = NULL, unsigned ilOffset = 0);
    HRESULT GetOrCreateActiveNativeCodeVersion(MethodDesc* pClosedMethodDesc, NativeCodeVersion* pNativeCodeVersion);
    HRESULT SetActiveNativeCodeVersion(NativeCodeVersion activeNativeCodeVersion);
#endif //DACCESS_COMPILE

    RejitFlags GetRejitState() const;
    BOOL GetEnableReJITCallback() const;
    BOOL IsDeoptimized() const;
#ifndef DACCESS_COMPILE
    void SetRejitState(RejitFlags newState);
    void SetEnableReJITCallback(BOOL state);
#endif

#ifdef DACCESS_COMPILE
    // The DAC is privy to the backing node abstraction
    PTR_ILCodeVersionNode AsNode() const;
#endif

private:

#ifndef DACCESS_COMPILE
    PTR_ILCodeVersionNode AsNode();
    PTR_ILCodeVersionNode AsNode() const;
#endif

    enum StorageKind
    {
        Unknown,
        Explicit,
        Synthetic
    };

    StorageKind m_storageKind;
    union
    {
        PTR_ILCodeVersionNode m_pVersionNode;
        struct
        {
            PTR_Module m_pModule;
            mdMethodDef m_methodDef;
        } m_synthetic;
    };

    // cDAC accesses fields via ILCodeVersioningState.m_activeVersion
    friend struct ::cdac_data<ILCodeVersioningState>;
};

class NativeCodeVersionNode
{
    friend NativeCodeVersionIterator;
    friend MethodDescVersioningState;
    friend ILCodeVersionNode;

public:
#ifndef DACCESS_COMPILE
    NativeCodeVersionNode(NativeCodeVersionId id, MethodDesc* pMethod, ReJITID parentId, NativeCodeVersion::OptimizationTier optimizationTier,
        PatchpointInfo* patchpointInfo, unsigned ilOffset);
#endif

    PTR_MethodDesc GetMethodDesc() const; // Can be called without any locks
    NativeCodeVersionId GetVersionId() const; // Can be called without any locks
    PCODE GetNativeCode() const; // Can be called without any locks, but result may be stale if it wasn't already set
    ReJITID GetILVersionId() const; // Can be called without any locks
    ILCodeVersion GetILCodeVersion() const;// Can be called without any locks
    BOOL IsActiveChildVersion() const;
#ifndef DACCESS_COMPILE
    BOOL SetNativeCodeInterlocked(PCODE pCode, PCODE pExpected);
    void SetActiveChildFlag(BOOL isActive);
#endif

#ifdef FEATURE_TIERED_COMPILATION
    NativeCodeVersion::OptimizationTier GetOptimizationTier() const;
#ifndef DACCESS_COMPILE
    void SetOptimizationTier(NativeCodeVersion::OptimizationTier tier);
#endif
#endif // FEATURE_TIERED_COMPILATION

#ifdef HAVE_GCCOVER
    PTR_GCCoverageInfo GetGCCoverageInfo() const;
    void SetGCCoverageInfo(PTR_GCCoverageInfo gcCover);
#endif

#ifdef FEATURE_ON_STACK_REPLACEMENT
    PatchpointInfo * GetOSRInfo(unsigned * ilOffset);
#endif

private:
    //union - could save a little memory?
    //{
    PCODE m_pNativeCode;
    DAC_IGNORE(const) PTR_MethodDesc m_pMethodDesc;
    //};

    DAC_IGNORE(const) ReJITID m_parentId;
    PTR_NativeCodeVersionNode m_pNextMethodDescSibling; // Never modified after being added to the linked list
    DAC_IGNORE(const) NativeCodeVersionId m_id;
#ifdef FEATURE_TIERED_COMPILATION
    NativeCodeVersion::OptimizationTier m_optTier; // Set in constructor, but as the JIT runs it may upgrade the optimization tier
#endif
#ifdef HAVE_GCCOVER
    PTR_GCCoverageInfo m_gcCover;
#endif
#ifdef FEATURE_ON_STACK_REPLACEMENT
    DAC_IGNORE(const) PTR_PatchpointInfo m_patchpointInfo;
    DAC_IGNORE(const) unsigned m_ilOffset;
#endif

    enum NativeCodeVersionNodeFlags
    {
        IsActiveChildFlag = 1
    };
    DWORD m_flags;

    friend struct ::cdac_data<NativeCodeVersionNode>;
};

template<>
struct cdac_data<NativeCodeVersionNode>
{
    static constexpr size_t Next = offsetof(NativeCodeVersionNode, m_pNextMethodDescSibling);
    static constexpr size_t MethodDesc = offsetof(NativeCodeVersionNode, m_pMethodDesc);
    static constexpr size_t NativeCode = offsetof(NativeCodeVersionNode, m_pNativeCode);
    static constexpr size_t Flags = offsetof(NativeCodeVersionNode, m_flags);
    static constexpr size_t ILVersionId = offsetof(NativeCodeVersionNode, m_parentId);
#ifdef HAVE_GCCOVER
    static constexpr size_t GCCoverageInfo = offsetof(NativeCodeVersionNode, m_gcCover);
#endif // HAVE_GCCOVER
};

class NativeCodeVersionCollection
{
    friend class NativeCodeVersionIterator;
public:
    NativeCodeVersionCollection(PTR_MethodDesc pMethodDescFilter, ILCodeVersion ilCodeFilter);
    NativeCodeVersionIterator Begin();
    NativeCodeVersionIterator End();

private:
    PTR_MethodDesc m_pMethodDescFilter;
    ILCodeVersion m_ilCodeFilter;
};

class NativeCodeVersionIterator : public Enumerator<const NativeCodeVersion, NativeCodeVersionIterator>
{
    friend class Enumerator<const NativeCodeVersion, NativeCodeVersionIterator>;

public:
    NativeCodeVersionIterator(NativeCodeVersionCollection* pCollection);
    CHECK Check() const { CHECK_OK; }

protected:
    const NativeCodeVersion & Get() const;
    void First();
    void Next();
    bool Equal(const NativeCodeVersionIterator &i) const;

    CHECK DoCheck() const { CHECK_OK; }

private:
    enum IterationStage
    {
        Initial,
        ImplicitCodeVersion,
        LinkedList,
        End
    };
    IterationStage m_stage;
    NativeCodeVersionCollection* m_pCollection;
    PTR_NativeCodeVersionNode m_pLinkedListCur;
    NativeCodeVersion m_cur;
};

class ILCodeVersionNode
{
public:
    ILCodeVersionNode();
#ifndef DACCESS_COMPILE
    ILCodeVersionNode(Module* pModule, mdMethodDef methodDef, ReJITID id, BOOL isDeoptimized);
#endif
    PTR_Module GetModule() const;
    mdMethodDef GetMethodDef() const;
    ReJITID GetVersionId() const;
    PTR_COR_ILMETHOD GetIL() const;
    DWORD GetJitFlags() const;
    const InstrumentedILOffsetMapping* GetInstrumentedILMap() const;
    RejitFlags GetRejitState() const;
    BOOL GetEnableReJITCallback() const;
    PTR_ILCodeVersionNode GetNextILVersionNode() const;
    BOOL IsDeoptimized() const;
#ifndef DACCESS_COMPILE
    void SetIL(COR_ILMETHOD* pIL);
    void SetJitFlags(DWORD flags);
    void SetInstrumentedILMap(SIZE_T cMap, COR_IL_MAP * rgMap);
    void SetRejitState(RejitFlags newState);
    void SetEnableReJITCallback(BOOL state);
    void SetNextILVersionNode(ILCodeVersionNode* pNextVersionNode);
#endif

private:
    const PTR_Module m_pModule;
    const mdMethodDef m_methodDef;
    const ReJITID m_rejitId;
    PTR_ILCodeVersionNode m_pNextILVersionNode; // Never modified after being added to the linked list
    Volatile<RejitFlags> m_rejitState;
    VolatilePtr<COR_ILMETHOD, PTR_COR_ILMETHOD> m_pIL;
    Volatile<DWORD> m_jitFlags;
    InstrumentedILOffsetMapping m_instrumentedILMap;
    BOOL m_deoptimized;

    friend struct ::cdac_data<ILCodeVersionNode>;
};

template<>
struct cdac_data<ILCodeVersionNode>
{
    static constexpr size_t VersionId = offsetof(ILCodeVersionNode, m_rejitId);
    static constexpr size_t Next = offsetof(ILCodeVersionNode, m_pNextILVersionNode);
    static constexpr size_t RejitState = offsetof(ILCodeVersionNode, m_rejitState);
};

class ILCodeVersionCollection
{
    friend class ILCodeVersionIterator;

public:
    ILCodeVersionCollection(PTR_Module pModule, mdMethodDef methodDef);
    ILCodeVersionIterator Begin();
    ILCodeVersionIterator End();

private:
    PTR_Module m_pModule;
    mdMethodDef m_methodDef;
};

class ILCodeVersionIterator : public Enumerator<const ILCodeVersion, ILCodeVersionIterator>
{
    friend class Enumerator<const ILCodeVersion, ILCodeVersionIterator>;

public:
    ILCodeVersionIterator();
    ILCodeVersionIterator(const ILCodeVersionIterator & iter);
    ILCodeVersionIterator(ILCodeVersionCollection* pCollection);
    CHECK Check() const { CHECK_OK; }

protected:
    const ILCodeVersion & Get() const;
    void First();
    void Next();
    bool Equal(const ILCodeVersionIterator &i) const;

    CHECK DoCheck() const { CHECK_OK; }

private:
    enum IterationStage
    {
        Initial,
        ImplicitCodeVersion,
        LinkedList,
        End
    };
    IterationStage m_stage;
    ILCodeVersion m_cur;
    PTR_ILCodeVersionNode m_pLinkedListCur;
    ILCodeVersionCollection* m_pCollection;
};

class MethodDescVersioningState
{
public:
    MethodDescVersioningState(PTR_MethodDesc pMethodDesc);
    PTR_MethodDesc GetMethodDesc() const;
    NativeCodeVersionId AllocateVersionId();
    PTR_NativeCodeVersionNode GetFirstVersionNode() const;

#ifndef DACCESS_COMPILE
    void LinkNativeCodeVersionNode(NativeCodeVersionNode* pNativeCodeVersionNode);
#endif // DACCESS_COMPILE

    //read-write data for the default native code version
    BOOL IsDefaultVersionActiveChild() const;
#ifndef DACCESS_COMPILE
    void SetDefaultVersionActiveChildFlag(BOOL isActive);
#endif

private:
    PTR_MethodDesc m_pMethodDesc;

    enum MethodDescVersioningStateFlags
    {
        IsDefaultVersionActiveChildFlag = 0x4
    };
    BYTE m_flags;
    NativeCodeVersionId m_nextId;
    PTR_NativeCodeVersionNode m_pFirstVersionNode;

    friend struct ::cdac_data<MethodDescVersioningState>;
};

template<>
struct cdac_data<MethodDescVersioningState>
{
    static constexpr size_t NativeCodeVersionNode = offsetof(MethodDescVersioningState, m_pFirstVersionNode);
    static constexpr size_t Flags = offsetof(MethodDescVersioningState, m_flags);
};

class ILCodeVersioningState
{
public:
    ILCodeVersioningState(PTR_Module pModule, mdMethodDef methodDef);
    ILCodeVersion GetActiveVersion() const;
    PTR_ILCodeVersionNode GetFirstVersionNode() const;
#ifndef DACCESS_COMPILE
    void SetActiveVersion(ILCodeVersion ilActiveCodeVersion);
    void LinkILCodeVersionNode(ILCodeVersionNode* pILCodeVersionNode);
#endif

    struct Key
    {
    public:
        Key();
        Key(PTR_Module pModule, mdMethodDef methodDef);
        size_t Hash() const;
        bool operator==(const Key & rhs) const;
    private:
        PTR_Module m_pModule;
        mdMethodDef m_methodDef;
    };

    Key GetKey() const;

private:
    ILCodeVersion m_activeVersion;
    PTR_ILCodeVersionNode m_pFirstVersionNode;
    PTR_Module m_pModule;
    mdMethodDef m_methodDef;

    friend struct ::cdac_data<ILCodeVersioningState>;
};

template<>
struct cdac_data<ILCodeVersioningState>
{
    static constexpr size_t FirstVersionNode = offsetof(ILCodeVersioningState, m_pFirstVersionNode);
    static constexpr size_t ActiveVersionKind = offsetof(ILCodeVersioningState, m_activeVersion.m_storageKind);
    static constexpr size_t ActiveVersionNode = offsetof(ILCodeVersioningState, m_activeVersion.m_pVersionNode);
    static constexpr size_t ActiveVersionModule = offsetof(ILCodeVersioningState, m_activeVersion.m_synthetic.m_pModule);
    static constexpr size_t ActiveVersionMethodDef = offsetof(ILCodeVersioningState, m_activeVersion.m_synthetic.m_methodDef);
};

class CodeVersionManager
{
    friend class ILCodeVersion;
    friend struct _DacGlobals;

    SVAL_DECL(BOOL, s_HasNonDefaultILVersions);

public:
    CodeVersionManager() = default;

    BOOL HasNonDefaultILVersions();
    ILCodeVersionCollection GetILCodeVersions(PTR_MethodDesc pMethod);
    ILCodeVersionCollection GetILCodeVersions(PTR_Module pModule, mdMethodDef methodDef);
    ILCodeVersion GetActiveILCodeVersion(PTR_MethodDesc pMethod);
    ILCodeVersion GetActiveILCodeVersion(PTR_Module pModule, mdMethodDef methodDef);
    ILCodeVersion GetILCodeVersion(PTR_MethodDesc pMethod, ReJITID rejitId);
    NativeCodeVersionCollection GetNativeCodeVersions(PTR_MethodDesc pMethod) const;
    NativeCodeVersion GetNativeCodeVersion(PTR_MethodDesc pMethod, PCODE codeStartAddress) const;
    PTR_ILCodeVersioningState GetILCodeVersioningState(PTR_Module pModule, mdMethodDef methodDef) const;
    PTR_MethodDescVersioningState GetMethodDescVersioningState(PTR_MethodDesc pMethod) const;

#ifndef DACCESS_COMPILE
    struct CodePublishError
    {
        Module* pModule;
        mdMethodDef methodDef;
        MethodDesc* pMethodDesc;
        HRESULT hrStatus;
    };

    HRESULT AddILCodeVersion(Module* pModule, mdMethodDef methodDef, ILCodeVersion* pILCodeVersion, BOOL isDeoptimized);
    HRESULT AddNativeCodeVersion(ILCodeVersion ilCodeVersion, MethodDesc* pClosedMethodDesc, NativeCodeVersion::OptimizationTier optimizationTier, NativeCodeVersion* pNativeCodeVersion,
        PatchpointInfo* patchpointInfo = NULL, unsigned ilOffset = 0);
    PCODE PublishVersionableCodeIfNecessary(
        MethodDesc* pMethodDesc,
        CallerGCMode callerGCMode,
        bool *doBackpatchRef,
        bool *doFullBackpatchRef);

private:
    HRESULT PublishNativeCodeVersion(MethodDesc* pMethodDesc, NativeCodeVersion nativeCodeVersion);
    HRESULT GetOrCreateMethodDescVersioningState(MethodDesc* pMethod, MethodDescVersioningState** ppMethodDescVersioningState);
    HRESULT GetOrCreateILCodeVersioningState(Module* pModule, mdMethodDef methodDef, ILCodeVersioningState** ppILCodeVersioningState);

public:
    HRESULT SetActiveILCodeVersions(ILCodeVersion* pActiveVersions, DWORD cActiveVersions, CDynArray<CodePublishError> * pPublishErrors);
    static HRESULT AddCodePublishError(Module* pModule, mdMethodDef methodDef, MethodDesc* pMD, HRESULT hrStatus, CDynArray<CodePublishError> * pErrors);
    static HRESULT AddCodePublishError(NativeCodeVersion nativeCodeVersion, HRESULT hrStatus, CDynArray<CodePublishError> * pErrors);
    static void OnAppDomainExit(AppDomain* pAppDomain);
#endif

    static bool IsMethodSupported(PTR_MethodDesc pMethodDesc);

#ifndef DACCESS_COMPILE
    static bool InitialNativeCodeVersionMayNotBeTheDefaultNativeCodeVersion()
    {
        LIMITED_METHOD_CONTRACT;
        return s_initialNativeCodeVersionMayNotBeTheDefaultNativeCodeVersion;
    }

    static void SetInitialNativeCodeVersionMayNotBeTheDefaultNativeCodeVersion()
    {
        LIMITED_METHOD_CONTRACT;
        s_initialNativeCodeVersionMayNotBeTheDefaultNativeCodeVersion = true;
    }
#endif

private:

#ifndef DACCESS_COMPILE
    static HRESULT EnumerateClosedMethodDescs(MethodDesc* pMD, CDynArray<MethodDesc*> * pClosedMethodDescs, CDynArray<CodePublishError> * pUnsupportedMethodErrors);
    static HRESULT EnumerateDomainClosedMethodDescs(
        AppDomain * pAppDomainToSearch,
        Module* pModuleContainingMethodDef,
        mdMethodDef methodDef,
        CDynArray<MethodDesc*> * pClosedMethodDescs,
        CDynArray<CodePublishError> * pUnsupportedMethodErrors);
    static HRESULT GetNonVersionableError(MethodDesc* pMD);
    void ReportCodePublishError(CodePublishError* pErrorRecord);
    void ReportCodePublishError(MethodDesc* pMD, HRESULT hrStatus);
    void ReportCodePublishError(Module* pModule, mdMethodDef methodDef, MethodDesc* pMD, HRESULT hrStatus);

    static bool s_initialNativeCodeVersionMayNotBeTheDefaultNativeCodeVersion;
#endif

private:
    static CrstStatic s_lock;

#ifndef DACCESS_COMPILE
public:
    static void StaticInitialize()
    {
        WRAPPER_NO_CONTRACT;

        s_lock.Init(
            CrstCodeVersioning,
            CrstFlags(CRST_UNSAFE_ANYMODE | CRST_DEBUGGER_THREAD | CRST_REENTRANCY | CRST_TAKEN_DURING_SHUTDOWN));
    }
#endif

#ifdef _DEBUG
public:
    static bool IsLockOwnedByCurrentThread();
#endif

public:
    class LockHolder : private CrstHolderWithState
    {
    public:
        LockHolder()
        #ifndef DACCESS_COMPILE
            : CrstHolderWithState(&s_lock)
        #else
            : CrstHolderWithState(nullptr)
        #endif
        {
            WRAPPER_NO_CONTRACT;
        }

        LockHolder(const LockHolder &) = delete;
        LockHolder &operator =(const LockHolder &) = delete;
    };
};

#endif // FEATURE_CODE_VERSIONING

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// NativeCodeVersion definitions

inline NativeCodeVersion::NativeCodeVersion()
#ifdef FEATURE_CODE_VERSIONING
    : m_storageKind(StorageKind::Unknown), m_pVersionNode(PTR_NULL)
#else
    : m_pMethodDesc(PTR_NULL)
#endif
{
    LIMITED_METHOD_DAC_CONTRACT;
#ifdef FEATURE_CODE_VERSIONING
    static_assert_no_msg(sizeof(m_pVersionNode) == sizeof(m_synthetic));
#endif
}

inline NativeCodeVersion::NativeCodeVersion(const NativeCodeVersion & rhs)
#ifdef FEATURE_CODE_VERSIONING
    : m_storageKind(rhs.m_storageKind), m_pVersionNode(rhs.m_pVersionNode)
#else
    : m_pMethodDesc(rhs.m_pMethodDesc)
#endif
{
    LIMITED_METHOD_DAC_CONTRACT;
#ifdef FEATURE_CODE_VERSIONING
    static_assert_no_msg(sizeof(m_pVersionNode) == sizeof(m_synthetic));
#endif
}

inline BOOL NativeCodeVersion::IsNull() const
{
    LIMITED_METHOD_DAC_CONTRACT;

#ifdef FEATURE_CODE_VERSIONING
    return m_storageKind == StorageKind::Unknown;
#else
    return m_pMethodDesc == NULL;
#endif
}

inline PTR_MethodDesc NativeCodeVersion::GetMethodDesc() const
{
    LIMITED_METHOD_DAC_CONTRACT;

#ifdef FEATURE_CODE_VERSIONING
    return m_storageKind == StorageKind::Explicit ? m_pVersionNode->GetMethodDesc() : m_synthetic.m_pMethodDesc;
#else
    return m_pMethodDesc;
#endif
}

inline NativeCodeVersionId NativeCodeVersion::GetVersionId() const
{
    LIMITED_METHOD_DAC_CONTRACT;

#ifdef FEATURE_CODE_VERSIONING
    if (m_storageKind == StorageKind::Explicit)
    {
        return m_pVersionNode->GetVersionId();
    }
#endif
    return 0;
}

inline bool NativeCodeVersion::operator==(const NativeCodeVersion & rhs) const
{
    LIMITED_METHOD_DAC_CONTRACT;

#ifdef FEATURE_CODE_VERSIONING
    static_assert_no_msg(sizeof(m_pVersionNode) == sizeof(m_synthetic));
    return m_storageKind == rhs.m_storageKind && m_pVersionNode == rhs.m_pVersionNode;
#else
    return m_pMethodDesc == rhs.m_pMethodDesc;
#endif
}

inline bool NativeCodeVersion::operator!=(const NativeCodeVersion & rhs) const
{
    WRAPPER_NO_CONTRACT;
    return !operator==(rhs);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// NativeCodeVersionNode definitions

#ifdef FEATURE_CODE_VERSIONING

inline PTR_MethodDesc NativeCodeVersionNode::GetMethodDesc() const
{
    LIMITED_METHOD_DAC_CONTRACT;
    return m_pMethodDesc;
}

#endif // FEATURE_CODE_VERSIONING

#endif // CODE_VERSION_H
