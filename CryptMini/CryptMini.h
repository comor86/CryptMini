
#include <fltKernel.h>
#include <dontuse.h>
#include <suppress.h>

#pragma prefast(disable:__WARNING_ENCODE_MEMBER_FUNCTION_POINTER, "Not valid for kernel mode drivers")


PFLT_FILTER gFilterHandle;
ULONG_PTR OperationStatusCtx = 1;

#define PTDBG_TRACE_ROUTINES            0x00000001
#define PTDBG_TRACE_OPERATION_STATUS    0x00000002

ULONG gTraceFlags = 1;


#define PT_DBG_PRINT( _dbgLevel, _string )          \
	(FlagOn(gTraceFlags, (_dbgLevel)) ? \
	DbgPrint _string : \
	((int)0))

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
CryptMiniUnload(
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
#pragma alloc_text(PAGE, CryptMiniUnload)
#pragma alloc_text(PAGE, CryptMiniInstanceQueryTeardown)
#pragma alloc_text(PAGE, CryptMiniInstanceSetup)
#pragma alloc_text(PAGE, CryptMiniInstanceTeardownStart)
#pragma alloc_text(PAGE, CryptMiniInstanceTeardownComplete)
#endif

//
//  operation registration
//

CONST FLT_OPERATION_REGISTRATION Callbacks[] = {

	{ IRP_MJ_CREATE,
	0,
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

	NULL,                               //  Context
	Callbacks,                          //  Operation callbacks

	CryptMiniUnload,                           //  MiniFilterUnload

	CryptMiniInstanceSetup,                    //  InstanceSetup
	CryptMiniInstanceQueryTeardown,            //  InstanceQueryTeardown
	CryptMiniInstanceTeardownStart,            //  InstanceTeardownStart
	CryptMiniInstanceTeardownComplete,         //  InstanceTeardownComplete

	NULL,                               //  GenerateFileName
	NULL,                               //  GenerateDestinationFileName
	NULL                                //  NormalizeNameComponent

};

