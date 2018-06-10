
#include <fltKernel.h>
#include <dontuse.h>
#include <suppress.h>

#include "common.h"
#include "ctx.h"

#pragma prefast(disable:__WARNING_ENCODE_MEMBER_FUNCTION_POINTER, "Not valid for kernel mode drivers")


PFLT_FILTER gFilterHandle;
ULONG_PTR OperationStatusCtx = 1;

#define LOG_INFO				0x00000001
#define LOG_ERROR				0x00000002
#define LOG_CREATE				0x00000004
#define LOG_READ				0x00000008
#define LOG_WRITE				0x00000010
#define LOG_DIRCTRL				0x00000020

ULONG gLogFlags = 3;		//调试信息输出开关


#define LOG_PRINT( _logFlag, _string )          \
	(FlagOn(gLogFlags, (_logFlag)) ? \
	DbgPrint _string : \
	((int)0))

/*************************************************************************
Pool Tags
*************************************************************************/
#define BUFFER_SWAP_TAG     'bdBS'
#define CONTEXT_TAG         'xcBS'
#define NAME_TAG            'mnBS'
#define PRE_2_POST_TAG      'ppBS'


/*************************************************************************
Local structures
*************************************************************************/
#define MIN_SECTOR_SIZE 0x200

//
//  This is a context structure that is used to pass state from our
//  pre-operation callback to our post-operation callback.
//
typedef struct _PRE_2_POST_CONTEXT {
	//
	//  Pointer to our volume context structure.  We always get the context
	//  in the preOperation path because you can not safely get it at DPC
	//  level.  We then release it in the postOperation path.  It is safe
	//  to release contexts at DPC level.
	//
	PVOLUME_CONTEXT VolCtx;

	PSTREAM_CONTEXT pStreamCtx;

	//
	//  Since the post-operation parameters always receive the "original"
	//  parameters passed to the operation, we need to pass our new destination
	//  buffer to our post operation routine so we can free it.
	//
	PVOID SwappedBuffer;

} PRE_2_POST_CONTEXT, *PPRE_2_POST_CONTEXT;
//
//  This is a lookAside list used to allocate our pre-2-post structure.
//
NPAGED_LOOKASIDE_LIST Pre2PostContextList;

/*************************************************************************
	框架定义函数
*************************************************************************/

DRIVER_INITIALIZE DriverEntry;

NTSTATUS
DriverEntry(
_In_ PDRIVER_OBJECT DriverObject,
_In_ PUNICODE_STRING RegistryPath
);

NTSTATUS
CryptMiniInstanceSetup(
_In_ PCFLT_RELATED_OBJECTS FltObjects,
_In_ FLT_INSTANCE_SETUP_FLAGS Flags,
_In_ DEVICE_TYPE VolumeDeviceType,
_In_ FLT_FILESYSTEM_TYPE VolumeFilesystemType
);

VOID
CryptMiniInstanceTeardownStart(
_In_ PCFLT_RELATED_OBJECTS FltObjects,
_In_ FLT_INSTANCE_TEARDOWN_FLAGS Flags
);

VOID
CryptMiniInstanceTeardownComplete(
_In_ PCFLT_RELATED_OBJECTS FltObjects,
_In_ FLT_INSTANCE_TEARDOWN_FLAGS Flags
);

NTSTATUS
DriveExit(
_In_ FLT_FILTER_UNLOAD_FLAGS Flags
);

NTSTATUS
CryptMiniInstanceQueryTeardown(
_In_ PCFLT_RELATED_OBJECTS FltObjects,
_In_ FLT_INSTANCE_QUERY_TEARDOWN_FLAGS Flags
);

FLT_PREOP_CALLBACK_STATUS
CryptMiniPreOperation(
_Inout_ PFLT_CALLBACK_DATA Data,
_In_ PCFLT_RELATED_OBJECTS FltObjects,
_Flt_CompletionContext_Outptr_ PVOID *CompletionContext
);

VOID
CryptMiniOperationStatusCallback(
_In_ PCFLT_RELATED_OBJECTS FltObjects,
_In_ PFLT_IO_PARAMETER_BLOCK ParameterSnapshot,
_In_ NTSTATUS OperationStatus,
_In_ PVOID RequesterContext
);

FLT_POSTOP_CALLBACK_STATUS
CryptMiniPostOperation(
_Inout_ PFLT_CALLBACK_DATA Data,
_In_ PCFLT_RELATED_OBJECTS FltObjects,
_In_opt_ PVOID CompletionContext,
_In_ FLT_POST_OPERATION_FLAGS Flags
);

FLT_PREOP_CALLBACK_STATUS
CryptMiniPreOperationNoPostOperation(
_Inout_ PFLT_CALLBACK_DATA Data,
_In_ PCFLT_RELATED_OBJECTS FltObjects,
_Flt_CompletionContext_Outptr_ PVOID *CompletionContext
);

BOOLEAN
CryptMiniDoRequestOperationStatus(
_In_ PFLT_CALLBACK_DATA Data
);

VOID
CleanupContext(
__in PFLT_CONTEXT Context,
__in FLT_CONTEXT_TYPE ContextType
);

/*************************************************************************
	自定义回调函数
*************************************************************************/

FLT_PREOP_CALLBACK_STATUS
PreCreate(
_Inout_ PFLT_CALLBACK_DATA Data,
_In_ PCFLT_RELATED_OBJECTS FltObjects,
_Flt_CompletionContext_Outptr_ PVOID *CompletionContext
);

FLT_POSTOP_CALLBACK_STATUS
PostCreate(
_Inout_ PFLT_CALLBACK_DATA Data,
_In_ PCFLT_RELATED_OBJECTS FltObjects,
_In_opt_ PVOID CompletionContext,
_In_ FLT_POST_OPERATION_FLAGS Flags
);

FLT_PREOP_CALLBACK_STATUS
PreRead(
_Inout_ PFLT_CALLBACK_DATA Data,
_In_ PCFLT_RELATED_OBJECTS FltObjects,
_Flt_CompletionContext_Outptr_ PVOID *CompletionContext
);

FLT_POSTOP_CALLBACK_STATUS
PostRead(
_Inout_ PFLT_CALLBACK_DATA Data,
_In_ PCFLT_RELATED_OBJECTS FltObjects,
_In_opt_ PVOID CompletionContext,
_In_ FLT_POST_OPERATION_FLAGS Flags
);

FLT_PREOP_CALLBACK_STATUS
PreWrite(
_Inout_ PFLT_CALLBACK_DATA Data,
_In_ PCFLT_RELATED_OBJECTS FltObjects,
_Flt_CompletionContext_Outptr_ PVOID *CompletionContext
);

FLT_POSTOP_CALLBACK_STATUS
PostWrite(
_Inout_ PFLT_CALLBACK_DATA Data,
_In_ PCFLT_RELATED_OBJECTS FltObjects,
_In_opt_ PVOID CompletionContext,
_In_ FLT_POST_OPERATION_FLAGS Flags
);

//
//  Assign text sections for each routine.
//

#ifdef ALLOC_PRAGMA
#pragma alloc_text(INIT, DriverEntry)
#pragma alloc_text(PAGE, DriveExit)
#pragma alloc_text(PAGE, CryptMiniInstanceQueryTeardown)
#pragma alloc_text(PAGE, CryptMiniInstanceSetup)
#pragma alloc_text(PAGE, CryptMiniInstanceTeardownStart)
#pragma alloc_text(PAGE, CryptMiniInstanceTeardownComplete)
#endif

//
//  Context definitions we currently care about.  Note that the system will
//  create a lookAside list for the volume context because an explicit size
//  of the context is specified.
//

CONST FLT_CONTEXT_REGISTRATION ContextArray[] = {

	{ FLT_VOLUME_CONTEXT, 0, CleanupContext, sizeof(VOLUME_CONTEXT), CONTEXT_TAG },

	{ FLT_STREAM_CONTEXT, 0, CleanupContext, STREAM_CONTEXT_SIZE, STREAM_CONTEXT_TAG },

	{ FLT_CONTEXT_END }
};

//
//  operation registration
//

CONST FLT_OPERATION_REGISTRATION Callbacks[] = {

	{ IRP_MJ_CREATE,
	FLTFL_OPERATION_REGISTRATION_SKIP_PAGING_IO,
	PreCreate,
	PostCreate },

	{ IRP_MJ_READ,
	0,
	PreRead,
	PostRead },

	{ IRP_MJ_WRITE,
	0,
	PreWrite,
	PostWrite },

#if 0 // TODO - List all of the requests to filter.

	{ IRP_MJ_CREATE_NAMED_PIPE,
	0,
	CryptMiniPreOperation,
	CryptMiniPostOperation },

	{ IRP_MJ_CLOSE,
	0,
	CryptMiniPreOperation,
	CryptMiniPostOperation },

	{ IRP_MJ_QUERY_INFORMATION,
	0,
	CryptMiniPreOperation,
	CryptMiniPostOperation },

	{ IRP_MJ_SET_INFORMATION,
	0,
	CryptMiniPreOperation,
	CryptMiniPostOperation },

	{ IRP_MJ_QUERY_EA,
	0,
	CryptMiniPreOperation,
	CryptMiniPostOperation },

	{ IRP_MJ_SET_EA,
	0,
	CryptMiniPreOperation,
	CryptMiniPostOperation },

	{ IRP_MJ_FLUSH_BUFFERS,
	0,
	CryptMiniPreOperation,
	CryptMiniPostOperation },

	{ IRP_MJ_QUERY_VOLUME_INFORMATION,
	0,
	CryptMiniPreOperation,
	CryptMiniPostOperation },

	{ IRP_MJ_SET_VOLUME_INFORMATION,
	0,
	CryptMiniPreOperation,
	CryptMiniPostOperation },

	{ IRP_MJ_DIRECTORY_CONTROL,
	0,
	CryptMiniPreOperation,
	CryptMiniPostOperation },

	{ IRP_MJ_FILE_SYSTEM_CONTROL,
	0,
	CryptMiniPreOperation,
	CryptMiniPostOperation },

	{ IRP_MJ_DEVICE_CONTROL,
	0,
	CryptMiniPreOperation,
	CryptMiniPostOperation },

	{ IRP_MJ_INTERNAL_DEVICE_CONTROL,
	0,
	CryptMiniPreOperation,
	CryptMiniPostOperation },

	{ IRP_MJ_SHUTDOWN,
	0,
	CryptMiniPreOperationNoPostOperation,
	NULL },                               //post operations not supported

	{ IRP_MJ_LOCK_CONTROL,
	0,
	CryptMiniPreOperation,
	CryptMiniPostOperation },

	{ IRP_MJ_CLEANUP,
	0,
	CryptMiniPreOperation,
	CryptMiniPostOperation },

	{ IRP_MJ_CREATE_MAILSLOT,
	0,
	CryptMiniPreOperation,
	CryptMiniPostOperation },

	{ IRP_MJ_QUERY_SECURITY,
	0,
	CryptMiniPreOperation,
	CryptMiniPostOperation },

	{ IRP_MJ_SET_SECURITY,
	0,
	CryptMiniPreOperation,
	CryptMiniPostOperation },

	{ IRP_MJ_QUERY_QUOTA,
	0,
	CryptMiniPreOperation,
	CryptMiniPostOperation },

	{ IRP_MJ_SET_QUOTA,
	0,
	CryptMiniPreOperation,
	CryptMiniPostOperation },

	{ IRP_MJ_PNP,
	0,
	CryptMiniPreOperation,
	CryptMiniPostOperation },

	{ IRP_MJ_ACQUIRE_FOR_SECTION_SYNCHRONIZATION,
	0,
	CryptMiniPreOperation,
	CryptMiniPostOperation },

	{ IRP_MJ_RELEASE_FOR_SECTION_SYNCHRONIZATION,
	0,
	CryptMiniPreOperation,
	CryptMiniPostOperation },

	{ IRP_MJ_ACQUIRE_FOR_MOD_WRITE,
	0,
	CryptMiniPreOperation,
	CryptMiniPostOperation },

	{ IRP_MJ_RELEASE_FOR_MOD_WRITE,
	0,
	CryptMiniPreOperation,
	CryptMiniPostOperation },

	{ IRP_MJ_ACQUIRE_FOR_CC_FLUSH,
	0,
	CryptMiniPreOperation,
	CryptMiniPostOperation },

	{ IRP_MJ_RELEASE_FOR_CC_FLUSH,
	0,
	CryptMiniPreOperation,
	CryptMiniPostOperation },

	{ IRP_MJ_FAST_IO_CHECK_IF_POSSIBLE,
	0,
	CryptMiniPreOperation,
	CryptMiniPostOperation },

	{ IRP_MJ_NETWORK_QUERY_OPEN,
	0,
	CryptMiniPreOperation,
	CryptMiniPostOperation },

	{ IRP_MJ_MDL_READ,
	0,
	CryptMiniPreOperation,
	CryptMiniPostOperation },

	{ IRP_MJ_MDL_READ_COMPLETE,
	0,
	CryptMiniPreOperation,
	CryptMiniPostOperation },

	{ IRP_MJ_PREPARE_MDL_WRITE,
	0,
	CryptMiniPreOperation,
	CryptMiniPostOperation },

	{ IRP_MJ_MDL_WRITE_COMPLETE,
	0,
	CryptMiniPreOperation,
	CryptMiniPostOperation },

	{ IRP_MJ_VOLUME_MOUNT,
	0,
	CryptMiniPreOperation,
	CryptMiniPostOperation },

	{ IRP_MJ_VOLUME_DISMOUNT,
	0,
	CryptMiniPreOperation,
	CryptMiniPostOperation },

#endif // TODO

	{ IRP_MJ_OPERATION_END }
};

//
//  This defines what we want to filter with FltMgr
//

CONST FLT_REGISTRATION FilterRegistration = {

	sizeof(FLT_REGISTRATION),         //  Size
	FLT_REGISTRATION_VERSION,           //  Version
	0,                                  //  Flags

	ContextArray,                               //  Context
	Callbacks,                          //  Operation callbacks

	DriveExit,                           //  MiniFilterUnload

	CryptMiniInstanceSetup,                    //  InstanceSetup
	CryptMiniInstanceQueryTeardown,            //  InstanceQueryTeardown
	CryptMiniInstanceTeardownStart,            //  InstanceTeardownStart
	CryptMiniInstanceTeardownComplete,         //  InstanceTeardownComplete

	NULL,                               //  GenerateFileName
	NULL,                               //  GenerateDestinationFileName
	NULL                                //  NormalizeNameComponent

};

