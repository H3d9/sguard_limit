#pragma once
#include <ntdef.h>


// basic types

typedef enum _MI_VAD_TYPE {
    VadNone,
    VadDevicePhysicalMemory,
    VadImageMap,
    VadAwe,
    VadWriteWatch,
    VadLargePages,
    VadRotatePhysical,
    VadLargePageSection
} MI_VAD_TYPE, *PMI_VAD_TYPE;

typedef union _EX_FAST_REF {
    void* Object;
    struct {
        unsigned __int64 RefCnt : 4;
    };
    unsigned __int64 Value;
} EX_FAST_REF, *PEX_FAST_REF;

typedef struct _CONTROL_AREA {
    struct _SEGMENT* Segment;
    struct _LIST_ENTRY ListHead;
    unsigned __int64 NumberOfSectionReferences;
    unsigned __int64 NumberOfPfnReferences;
    unsigned __int64 NumberOfMappedViews;
    unsigned __int64 NumberOfUserReferences;
    unsigned long f1;
    unsigned long f2;
    EX_FAST_REF FilePointer;
    // Other fields
} CONTROL_AREA, *PCONTROL_AREA;

typedef struct _SUBSECTION {
    PCONTROL_AREA ControlArea;
    // Other fields
} SUBSECTION, *PSUBSECTION;


// Vad in NT 6.1

struct _MMVAD_FLAGS_7 {
    unsigned __int64 CommitCharge : 51; // Size=8 Offset=0 BitOffset=0 BitCount=51
    unsigned __int64 NoChange : 1; // Size=8 Offset=0 BitOffset=51 BitCount=1
    unsigned __int64 VadType : 3; // Size=8 Offset=0 BitOffset=52 BitCount=3
    unsigned __int64 MemCommit : 1; // Size=8 Offset=0 BitOffset=55 BitCount=1
    unsigned __int64 Protection : 5; // Size=8 Offset=0 BitOffset=56 BitCount=5
    unsigned __int64 Spare : 2; // Size=8 Offset=0 BitOffset=61 BitCount=2
    unsigned __int64 PrivateMemory : 1; // Size=8 Offset=0 BitOffset=63 BitCount=1
};

union ___unnamed712 {
    unsigned __int64 LongFlags; // Size=8 Offset=0
    struct _MMVAD_FLAGS_7 VadFlags; // Size=8 Offset=0
};

typedef struct _MMVAD_7 {
    unsigned __int64 u1; // Size=8 Offset=0
    struct _MMVAD_7* LeftChild; // Size=8 Offset=8
    struct _MMVAD_7* RightChild; // Size=8 Offset=16
    unsigned __int64 StartingVpn; // Size=8 Offset=24
    unsigned __int64 EndingVpn; // Size=8 Offset=32
    union ___unnamed712 u; // Size=8 Offset=40
    void* PushLock; // Size=8 Offset=48
    unsigned __int64 u5; // Size=8 Offset=56
    unsigned long u2; // Size=4 Offset=64
    unsigned long pad0; // Size=4 Offset=68
    struct _SUBSECTION* Subsection; // Size=8 Offset=72
    // Other fields
} MMVAD_7, *PMMVAD_7;

struct _MMADDRESS_NODE {
    unsigned __int64 u1;
    struct _MMADDRESS_NODE* LeftChild; // Size=8 Offset=8
    struct _MMADDRESS_NODE* RightChild; // Size=8 Offset=16
    // Other fields
};

typedef struct _MM_AVL_TABLE_7 {
    struct _MMADDRESS_NODE BalancedRoot; // Size=40 Offset=0
    struct {
        unsigned __int64 DepthOfTree : 5; // Size=8 Offset=40 BitOffset=0 BitCount=5
        unsigned __int64 Unused : 3; // Size=8 Offset=40 BitOffset=5 BitCount=3
        unsigned __int64 NumberGenericTableElements : 56; // Size=8 Offset=40 BitOffset=8 BitCount=56
    };
    // Other fields
} MM_AVL_TABLE_7, *PMM_AVL_TABLE_7;


// Vad in NT 6.2

struct _MMVAD_FLAGS_8 {
    unsigned long VadType : 3; // Size=4 Offset=0 BitOffset=0 BitCount=3
    unsigned long Protection : 5; // Size=4 Offset=0 BitOffset=3 BitCount=5
    unsigned long PreferredNode : 6; // Size=4 Offset=0 BitOffset=8 BitCount=6
    unsigned long NoChange : 1; // Size=4 Offset=0 BitOffset=14 BitCount=1
    unsigned long PrivateMemory : 1; // Size=4 Offset=0 BitOffset=15 BitCount=1
    unsigned long Teb : 1; // Size=4 Offset=0 BitOffset=16 BitCount=1
    unsigned long PrivateFixup : 1; // Size=4 Offset=0 BitOffset=17 BitCount=1
    unsigned long Spare : 13; // Size=4 Offset=0 BitOffset=18 BitCount=13
    unsigned long DeleteInProgress : 1; // Size=4 Offset=0 BitOffset=31 BitCount=1
};

union ___unnamed1784 {
    unsigned long LongFlags; // Size=8 Offset=0
    struct _MMVAD_FLAGS_8 VadFlags; // Size=8 Offset=0
};

struct _MM_AVL_NODE {
    unsigned __int64 u1; // Size=8 Offset=0
    struct _MM_AVL_NODE* LeftChild; // Size=8 Offset=8
    struct _MM_AVL_NODE* RightChild; // Size=8 Offset=16
};

struct _MMVAD_SHORT_8 {
    struct _MM_AVL_NODE VadNode; // Size=24 Offset=0
    unsigned long StartingVpn; // Size=4 Offset=24
    unsigned long EndingVpn; // Size=4 Offset=28
    void* PushLock; // Size=8 Offset=32
    union ___unnamed1784 u; // Size=4 Offset=40
    unsigned long u1; // Size=4 Offset=44
    void* EventList; // Size=8 Offset=48
    long ReferenceCount; // Size=4 Offset=56
};

typedef struct _MMVAD_8 {
    struct _MMVAD_SHORT_8 Core; // Size=64 Offset=0
    unsigned long u2; // Size=4 Offset=64
    unsigned long pad0;  // Size=4 Offset=68
    struct _SUBSECTION* Subsection; // Size=8 Offset=72
    // Other fields
} MMVAD_8, *PMMVAD_8;

typedef struct _MM_AVL_TABLE_8 {
    struct _MM_AVL_NODE BalancedRoot; // Size=24 Offset=0
    struct {
        unsigned __int64 DepthOfTree : 5; // Size=8 Offset=24 BitOffset=0 BitCount=5
        unsigned __int64 TableType : 3; // Size=8 Offset=24 BitOffset=5 BitCount=3
        unsigned __int64 NumberGenericTableElements : 56; // Size=8 Offset=24 BitOffset=8 BitCount=56
    };
    // Other fields
} MM_AVL_TABLE_8, *PMM_AVL_TABLE_8;


// Vad in NT 6.3 & NT 10.0

struct _MMVAD_FLAGS_10_17763 { // 9600 & 10240 ~ 17763
    unsigned long VadType : 3; // Size=4 Offset=0 BitOffset=0 BitCount=3
    unsigned long Protection : 5; // Size=4 Offset=0 BitOffset=3 BitCount=5
    unsigned long PreferredNode : 6; // Size=4 Offset=0 BitOffset=8 BitCount=6
    unsigned long NoChange : 1; // Size=4 Offset=0 BitOffset=14 BitCount=1
    unsigned long PrivateMemory : 1; // Size=4 Offset=0 BitOffset=15 BitCount=1
    unsigned long Teb : 1; // Size=4 Offset=0 BitOffset=16 BitCount=1
    unsigned long PrivateFixup : 1; // Size=4 Offset=0 BitOffset=17 BitCount=1
    unsigned long ManySubsections : 1; // Size=4 Offset=0 BitOffset=18 BitCount=1  // win8.1: is Spare
    unsigned long Spare : 12; // Size=4 Offset=0 BitOffset=19 BitCount=12
    unsigned long DeleteInProgress : 1; // Size=4 Offset=0 BitOffset=31 BitCount=1
}; // length = 4(0x4)

struct _MMVAD_FLAGS_10_18362 { // 18362 ~ 22000
    /*Offset=0x0000:0,BitLen=1*/ unsigned long Lock : 1;
    /*Offset=0x0000:1,BitLen=1*/ unsigned long LockContended : 1;
    /*Offset=0x0000:2,BitLen=1*/ unsigned long DeleteInProgress : 1;
    /*Offset=0x0000:3,BitLen=1*/ unsigned long NoChange : 1;
    /*Offset=0x0000:4,BitLen=3*/ unsigned long VadType : 3;
    /*Offset=0x0000:7,BitLen=5*/ unsigned long Protection : 5;
    /*Offset=0x0000:12,BitLen=7*/ unsigned long PreferredNode : 7;
    /*Offset=0x0000:19,BitLen=2*/ unsigned long PageSize : 2;
    /*Offset=0x0000:21,BitLen=1*/ unsigned long PrivateMemory : 1;
}; // length = 4(0x4)

union _MMVAD_FLAGS_10 {
    struct _MMVAD_FLAGS_10_17763 _17763;
    struct _MMVAD_FLAGS_10_18362 _18362;
};

union ___unnamed1951 {
    unsigned long LongFlags; // Size=4 Offset=0
    union _MMVAD_FLAGS_10 VadFlags; // Size=4 Offset=0
};

struct _MMVAD_SHORT_10 {
    union {
        struct _RTL_BALANCED_NODE VadNode; // Size=24 Offset=0
        struct _MMVAD_SHORT_10* NextVad; // Size=8 Offset=0
    };
    unsigned long StartingVpn; // Size=4 Offset=24
    unsigned long EndingVpn; // Size=4 Offset=28
    unsigned char StartingVpnHigh; // Size=1 Offset=32
    unsigned char EndingVpnHigh; // Size=1 Offset=33
    unsigned char CommitChargeHigh; // Size=1 Offset=34
    unsigned char SpareNT64VadUChar; // Size=1 Offset=35
    long ReferenceCount; // Size=4 Offset=36
    void* PushLock; // Size=8 Offset=40
    union ___unnamed1951 u; // Size=4 Offset=48
    unsigned long u1; // Size=4 Offset=52
    void* EventList; // Size=8 Offset=56
};

typedef struct _MMVAD_10 {
    struct _MMVAD_SHORT_10 Core; // Size=64 Offset=0
    unsigned long u2; // Size=4 Offset=64
    unsigned long pad0;  // Size=4 Offset=68
    struct _SUBSECTION* Subsection; // Size=8 Offset=72
    // Other fields
} MMVAD_10, *PMMVAD_10;

typedef struct _RTL_AVL_TREE {
    struct _RTL_BALANCED_NODE* Root;
} RTL_AVL_TREE, *PRTL_AVL_TREE;