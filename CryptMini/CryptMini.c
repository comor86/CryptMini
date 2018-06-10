/*++

Module Name:

    CryptMini.c

Abstract:

    This is the main module of the CryptMini miniFilter driver.

Environment:

    Kernel mode

--*/
#include "CryptMini.h"


/*************************************************************************
MiniFilter initialization and unload routines.
*************************************************************************/

NTSTATUS
DriverEntry(
_In_ PDRIVER_OBJECT DriverObject,
_In_ PUNICODE_STRING RegistryPath
)
/*++

Routine Description:

This is the initialization routine for this miniFilter driver.  This
registers with FltMgr and initializes all global data structures.

Arguments:

DriverObject - Pointer to driver object created by the system to
represent this driver.

RegistryPath - Unicode string identifying where the parameters for this
driver are located in the registry.

Return Value:

Routine can return non success error codes.

--*/
{
	NTSTATUS status;

	UNREFERENCED_PARAMETER(RegistryPath);

	LOG_PRINT(LOG_INFO,
		("[CryptMini]DriverEntry: Entered\n"));

	//  Pre2PostContextList结构体初始化
	ExInitializeNPagedLookasideList(&Pre2PostContextList, NULL, NULL, 0, sizeof(PRE_2_POST_CONTEXT), PRE_2_POST_TAG, 0);


	//注册minifilter
	status = FltRegisterFilter(DriverObject,
		&FilterRegistration,
		&gFilterHandle);

	FLT_ASSERT(NT_SUCCESS(status));

	if (NT_SUCCESS(status)) 
	{
		//开启过滤之前创建与R3通信端口
//		status = Msg_CreateCommunicationPort(gFilterHandle);
		if (NT_SUCCESS(status))
		{
			//开启过滤
			status = FltStartFiltering(gFilterHandle);

			if (!NT_SUCCESS(status)) {

				FltUnregisterFilter(gFilterHandle);
			}
		}
	}

	return status;
}

NTSTATUS
DriveExit(
_In_ FLT_FILTER_UNLOAD_FLAGS Flags
)
/*++

Routine Description:

This is the unload routine for this miniFilter driver. This is called
when the minifilter is about to be unloaded. We can fail this unload
request if this is not a mandatory unload indicated by the Flags
parameter.

Arguments:

Flags - Indicating if this is a mandatory unload.

Return Value:

Returns STATUS_SUCCESS.

--*/
{
	UNREFERENCED_PARAMETER(Flags);

	PAGED_CODE();

	LOG_PRINT(LOG_INFO,
		("[CryptMini]DriveExit: Entered\n"));

	//Close server port, must before filter is unregistered, otherwise filter will be halted.
//	Msg_CloseCommunicationPort(g_pServerPort);

	LOG_PRINT(LOG_INFO,
		("[CryptMini]DriveExit: FltUnregisterFilter\n"));
	FltUnregisterFilter(gFilterHandle);

	//Delete lookaside list
	LOG_PRINT(LOG_INFO,
		("[CryptMini]DriveExit: ExDeleteNPagedLookasideList\n"));
	ExDeleteNPagedLookasideList(&Pre2PostContextList);

	return STATUS_SUCCESS;
}



NTSTATUS
CryptMiniInstanceSetup (
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ FLT_INSTANCE_SETUP_FLAGS Flags,
    _In_ DEVICE_TYPE VolumeDeviceType,
    _In_ FLT_FILESYSTEM_TYPE VolumeFilesystemType
    )
/*++

Routine Description:

    This routine is called whenever a new instance is created on a volume. This
    gives us a chance to decide if we need to attach to this volume or not.

    If this routine is not defined in the registration structure, automatic
    instances are always created.

Arguments:

    FltObjects - Pointer to the FLT_RELATED_OBJECTS data structure containing
        opaque handles to this filter, instance and its associated volume.

    Flags - Flags describing the reason for this attach request.

Return Value:

    STATUS_SUCCESS - attach
    STATUS_FLT_DO_NOT_ATTACH - do not attach

--*/
{
	UNREFERENCED_PARAMETER(Flags);
	UNREFERENCED_PARAMETER(VolumeDeviceType);
	UNREFERENCED_PARAMETER(VolumeFilesystemType);

	PAGED_CODE();

	LOG_PRINT(LOG_INFO,
		("[CryptMini]CryptMiniInstanceSetup: Entered\n"));

	NTSTATUS status = STATUS_SUCCESS;
	PDEVICE_OBJECT devObj = NULL;
	PVOLUME_CONTEXT ctx = NULL;
	ULONG retLen;
	PUNICODE_STRING workingName;
	USHORT size;
	UCHAR volPropBuffer[sizeof(FLT_VOLUME_PROPERTIES) + 512];
	PFLT_VOLUME_PROPERTIES volProp = (PFLT_VOLUME_PROPERTIES)volPropBuffer;

	UCHAR szIV[16] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1, 2, 3, 4, 5, 6 };
	UCHAR szKey[MAX_KEY_LENGTH] = { 0 };
	UCHAR szKeyDigest[HASH_SIZE] = { 0 };
	UCHAR uKeyLen = 32;

	try {

		//Allocate a volume context structure.
		status = FltAllocateContext(FltObjects->Filter, FLT_VOLUME_CONTEXT, sizeof(VOLUME_CONTEXT), NonPagedPool, &ctx);
		if (!NT_SUCCESS(status))
			leave;

		//Always get the volume properties, so I can get a sector size
		status = FltGetVolumeProperties(FltObjects->Volume, volProp, sizeof(volPropBuffer), &retLen);
		if (!NT_SUCCESS(status))
			leave;

		ASSERT((volProp->SectorSize == 0) || (volProp->SectorSize >= MIN_SECTOR_SIZE));
		ctx->SectorSize = max(volProp->SectorSize, MIN_SECTOR_SIZE);
		ctx->Name.Buffer = NULL;
		ctx->FsName.Buffer = NULL;
		///ctx->aes_ctr_ctx = NULL ;

		//Get the storage device object we want a name for.
		status = FltGetDiskDeviceObject(FltObjects->Volume, &devObj);
		if (NT_SUCCESS(status))
		{
			status = RtlVolumeDeviceToDosName(devObj, &ctx->Name); //Try and get the DOS name.
		}

		//If we could not get a DOS name, get the NT name.
		if (!NT_SUCCESS(status))
		{
			LOG_PRINT(LOG_ERROR,
				("[CryptMini]CryptMiniInstanceSetup:  RtlVolumeDeviceToDosName failed\n"));

			ASSERT(ctx->Name.Buffer == NULL);

			//Figure out which name to use from the properties
			if (volProp->RealDeviceName.Length > 0)
			{
				workingName = &volProp->RealDeviceName;
				LOG_PRINT(LOG_ERROR,
					("[CryptMini]CryptMiniInstanceSetup: RealDeviceName %wZ\n", &ctx->Name));
			}
			else if (volProp->FileSystemDeviceName.Length > 0)
			{
				workingName = &volProp->FileSystemDeviceName;
				LOG_PRINT(LOG_ERROR,
					("[CryptMini]CryptMiniInstanceSetup: FileSystemDeviceName %wZ\n", &ctx->Name));
			}
			else
			{
				status = STATUS_FLT_DO_NOT_ATTACH;  //No name, don't save the context
				leave;
			}

			size = workingName->Length + sizeof(WCHAR); //length plus a trailing colon
			ctx->Name.Buffer = ExAllocatePoolWithTag(NonPagedPool, size, NAME_TAG);
			if (ctx->Name.Buffer == NULL)
			{
				status = STATUS_INSUFFICIENT_RESOURCES;
				leave;
			}
			ctx->Name.Length = 0;
			ctx->Name.MaximumLength = size;
			RtlCopyUnicodeString(&ctx->Name, workingName);
			RtlAppendUnicodeToString(&ctx->Name, L":");
		}

		//init aes ctr context
		KeInitializeSpinLock(&ctx->FsCryptSpinLock);
		///ctx->aes_ctr_ctx = counter_mode_ctx_init(szIV, g_szCurFileKey, uKeyLen) ;
		///if (NULL == ctx->aes_ctr_ctx)
		///{
		///	leave  ;
		///}


		//init per-volume mutex
		ExInitializeFastMutex(&ctx->FsCtxTableMutex);

		//Set the context
		status = FltSetVolumeContext(FltObjects->Volume, FLT_SET_CONTEXT_KEEP_IF_EXISTS, ctx, NULL);
		if (status == STATUS_FLT_CONTEXT_ALREADY_DEFINED) //It is OK for the context to already be defined.
			status = STATUS_SUCCESS;

		LOG_PRINT(LOG_ERROR,
			("[CryptMini]CryptMiniInstanceSetup:  %wZ\n", &ctx->Name));
	}
	finally {

		if (ctx)
		{
			FltReleaseContext(ctx); // system will hang if not call this routine
#if DBG			
			if ((ctx->Name.Buffer[0] == L'C') || (ctx->Name.Buffer[0] == L'c'))
			{
				FltDeleteContext(ctx);
				status = STATUS_FLT_DO_NOT_ATTACH;
				LOG_PRINT(LOG_ERROR,
					("[CryptMini]CryptMiniInstanceSetup:  system volume detach\n"));
			}
#endif
		}

		if (devObj)
			ObDereferenceObject(devObj);//Remove the reference added by FltGetDiskDeviceObject.
	}

    return status;
}


NTSTATUS
CryptMiniInstanceQueryTeardown (
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ FLT_INSTANCE_QUERY_TEARDOWN_FLAGS Flags
    )
/*++

Routine Description:

    This is called when an instance is being manually deleted by a
    call to FltDetachVolume or FilterDetach thereby giving us a
    chance to fail that detach request.

    If this routine is not defined in the registration structure, explicit
    detach requests via FltDetachVolume or FilterDetach will always be
    failed.

Arguments:

    FltObjects - Pointer to the FLT_RELATED_OBJECTS data structure containing
        opaque handles to this filter, instance and its associated volume.

    Flags - Indicating where this detach request came from.

Return Value:

    Returns the status of this operation.

--*/
{
    UNREFERENCED_PARAMETER( FltObjects );
    UNREFERENCED_PARAMETER( Flags );

    PAGED_CODE();

    LOG_PRINT( LOG_INFO,
                  ("[CryptMini]CryptMiniInstanceQueryTeardown: Entered\n") );

    return STATUS_SUCCESS;
}


VOID
CryptMiniInstanceTeardownStart (
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ FLT_INSTANCE_TEARDOWN_FLAGS Flags
    )
/*++

Routine Description:

    This routine is called at the start of instance teardown.

Arguments:

    FltObjects - Pointer to the FLT_RELATED_OBJECTS data structure containing
        opaque handles to this filter, instance and its associated volume.

    Flags - Reason why this instance is being deleted.

Return Value:

    None.

--*/
{
    UNREFERENCED_PARAMETER( FltObjects );
    UNREFERENCED_PARAMETER( Flags );

    PAGED_CODE();

    LOG_PRINT( LOG_INFO,
                  ("[CryptMini]CryptMiniInstanceTeardownStart: Entered\n") );
}


VOID
CryptMiniInstanceTeardownComplete (
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ FLT_INSTANCE_TEARDOWN_FLAGS Flags
    )
/*++

Routine Description:

    This routine is called at the end of instance teardown.

Arguments:

    FltObjects - Pointer to the FLT_RELATED_OBJECTS data structure containing
        opaque handles to this filter, instance and its associated volume.

    Flags - Reason why this instance is being deleted.

Return Value:

    None.

--*/
{
    UNREFERENCED_PARAMETER( FltObjects );
    UNREFERENCED_PARAMETER( Flags );

    PAGED_CODE();

    LOG_PRINT( LOG_INFO,
                  ("[CryptMini]CryptMiniInstanceTeardownComplete: Entered\n") );
}


VOID
CleanupContext(
__in PFLT_CONTEXT Context,
__in FLT_CONTEXT_TYPE ContextType
)
/*++

Routine Description:

The given context is being freed.
Free the allocated name buffer if there one.

Arguments:

Context - The context being freed

ContextType - The type of context this is

Return Value:

None

--*/
{
	PVOLUME_CONTEXT ctx = NULL;
	PSTREAM_CONTEXT streamCtx = NULL;

	PAGED_CODE();

	UNREFERENCED_PARAMETER(ContextType);

	//ASSERT(ContextType == FLT_VOLUME_CONTEXT);
	LOG_PRINT(LOG_INFO,
		("[CryptMini]CleanupContext: Entered\n"));

	switch (ContextType)
	{
	case FLT_VOLUME_CONTEXT:
	{
		LOG_PRINT(LOG_INFO,
			("[CryptMini]CleanupContext: FLT_VOLUME_CONTEXT\n"));

		ctx = (PVOLUME_CONTEXT)Context;

		if (ctx->Name.Buffer != NULL)
		{
			LOG_PRINT(LOG_INFO,
				("[CryptMini]CleanupContext: %wZ\n",&ctx->Name));
			ExFreePool(ctx->Name.Buffer);
			ctx->Name.Buffer = NULL;
		}

		///if (NULL != ctx->aes_ctr_ctx)
		///{
		///	counter_mode_ctx_destroy(ctx->aes_ctr_ctx) ;
		///	ctx->aes_ctr_ctx = NULL ;
		///}
	}
	break;
	case FLT_STREAM_CONTEXT:
	{
		LOG_PRINT(LOG_INFO,
			("[CryptMini]CleanupContext: FLT_STREAM_CONTEXT\n"));

		KIRQL OldIrql;

		streamCtx = (PSTREAM_CONTEXT)Context;

		if (streamCtx == NULL)
			break;

		if (streamCtx->FileName.Buffer != NULL)
		{
			ExFreePoolWithTag(streamCtx->FileName.Buffer, STRING_TAG);

			streamCtx->FileName.Length = streamCtx->FileName.MaximumLength = 0;
			streamCtx->FileName.Buffer = NULL;
		}

		///if (NULL != streamCtx->aes_ctr_ctx)
		///{
		///	counter_mode_ctx_destroy(streamCtx->aes_ctr_ctx) ;
		///	streamCtx->aes_ctr_ctx = NULL ;
		///}

		if (NULL != streamCtx->Resource)
		{
			ExDeleteResourceLite(streamCtx->Resource);
			ExFreePoolWithTag(streamCtx->Resource, RESOURCE_TAG);
		}
	}
	break;
	}
}

/*************************************************************************
    MiniFilter 自定义回调函数
*************************************************************************/

FLT_PREOP_CALLBACK_STATUS
PreCreate(
_Inout_ PFLT_CALLBACK_DATA Data,
_In_ PCFLT_RELATED_OBJECTS FltObjects,
_Flt_CompletionContext_Outptr_ PVOID *CompletionContext
)
{
	UNREFERENCED_PARAMETER(Data);
	UNREFERENCED_PARAMETER(FltObjects);
	UNREFERENCED_PARAMETER(CompletionContext);

	LOG_PRINT(LOG_INFO,
		("[CryptMini]PreCreate: Entered\n"));

	return FLT_PREOP_SUCCESS_WITH_CALLBACK;
}

FLT_POSTOP_CALLBACK_STATUS
PostCreate(
_Inout_ PFLT_CALLBACK_DATA Data,
_In_ PCFLT_RELATED_OBJECTS FltObjects,
_In_opt_ PVOID CompletionContext,
_In_ FLT_POST_OPERATION_FLAGS Flags
)
{
	UNREFERENCED_PARAMETER(Data);
	UNREFERENCED_PARAMETER(FltObjects);
	UNREFERENCED_PARAMETER(CompletionContext);
	UNREFERENCED_PARAMETER(Flags);

	LOG_PRINT(LOG_INFO,
		("[CryptMini]PostCreate: Entered\n"));

	return FLT_POSTOP_FINISHED_PROCESSING;
}

FLT_PREOP_CALLBACK_STATUS
PreRead(
_Inout_ PFLT_CALLBACK_DATA Data,
_In_ PCFLT_RELATED_OBJECTS FltObjects,
_Flt_CompletionContext_Outptr_ PVOID *CompletionContext
)
{
	UNREFERENCED_PARAMETER(Data);
	UNREFERENCED_PARAMETER(FltObjects);
	UNREFERENCED_PARAMETER(CompletionContext);

	LOG_PRINT(LOG_INFO,
		("[CryptMini]PreRead: Entered\n"));

	return FLT_PREOP_SUCCESS_WITH_CALLBACK;
}

FLT_POSTOP_CALLBACK_STATUS
PostRead(
_Inout_ PFLT_CALLBACK_DATA Data,
_In_ PCFLT_RELATED_OBJECTS FltObjects,
_In_opt_ PVOID CompletionContext,
_In_ FLT_POST_OPERATION_FLAGS Flags
)
{
	UNREFERENCED_PARAMETER(Data);
	UNREFERENCED_PARAMETER(FltObjects);
	UNREFERENCED_PARAMETER(CompletionContext);
	UNREFERENCED_PARAMETER(Flags);

	LOG_PRINT(LOG_INFO,
		("[CryptMini]PostRead: Entered\n"));

	return FLT_POSTOP_FINISHED_PROCESSING;
}

FLT_PREOP_CALLBACK_STATUS
PreWrite(
_Inout_ PFLT_CALLBACK_DATA Data,
_In_ PCFLT_RELATED_OBJECTS FltObjects,
_Flt_CompletionContext_Outptr_ PVOID *CompletionContext
)
{
	UNREFERENCED_PARAMETER(Data);
	UNREFERENCED_PARAMETER(FltObjects);
	UNREFERENCED_PARAMETER(CompletionContext);

	LOG_PRINT(LOG_INFO,
		("[CryptMini]PreWrite: Entered\n"));

	return FLT_PREOP_SUCCESS_WITH_CALLBACK;
}

FLT_POSTOP_CALLBACK_STATUS
PostWrite(
_Inout_ PFLT_CALLBACK_DATA Data,
_In_ PCFLT_RELATED_OBJECTS FltObjects,
_In_opt_ PVOID CompletionContext,
_In_ FLT_POST_OPERATION_FLAGS Flags
)
{
	UNREFERENCED_PARAMETER(Data);
	UNREFERENCED_PARAMETER(FltObjects);
	UNREFERENCED_PARAMETER(CompletionContext);
	UNREFERENCED_PARAMETER(Flags);

	LOG_PRINT(LOG_INFO,
		("[CryptMini]PostWrite: Entered\n"));

	return FLT_POSTOP_FINISHED_PROCESSING;
}

/*************************************************************************
MiniFilter callback routines.
*************************************************************************/
FLT_PREOP_CALLBACK_STATUS
CryptMiniPreOperation (
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _Flt_CompletionContext_Outptr_ PVOID *CompletionContext
    )
/*++

Routine Description:

    This routine is a pre-operation dispatch routine for this miniFilter.

    This is non-pageable because it could be called on the paging path

Arguments:

    Data - Pointer to the filter callbackData that is passed to us.

    FltObjects - Pointer to the FLT_RELATED_OBJECTS data structure containing
        opaque handles to this filter, instance, its associated volume and
        file object.

    CompletionContext - The context for the completion routine for this
        operation.

Return Value:

    The return value is the status of the operation.

--*/
{
    NTSTATUS status;

    UNREFERENCED_PARAMETER( FltObjects );
    UNREFERENCED_PARAMETER( CompletionContext );

    LOG_PRINT( LOG_INFO,
                  ("[CryptMini]CryptMiniPreOperation: Entered\n") );

    //
    //  See if this is an operation we would like the operation status
    //  for.  If so request it.
    //
    //  NOTE: most filters do NOT need to do this.  You only need to make
    //        this call if, for example, you need to know if the oplock was
    //        actually granted.
    //

    if (CryptMiniDoRequestOperationStatus( Data )) {

        status = FltRequestOperationStatusCallback( Data,
                                                    CryptMiniOperationStatusCallback,
                                                    (PVOID)(++OperationStatusCtx) );
        if (!NT_SUCCESS(status)) {

            LOG_PRINT( LOG_INFO,
                          ("[CryptMini]CryptMiniPreOperation: FltRequestOperationStatusCallback Failed, status=%08x\n",
                           status) );
        }
    }

    // This template code does not do anything with the callbackData, but
    // rather returns FLT_PREOP_SUCCESS_WITH_CALLBACK.
    // This passes the request down to the next miniFilter in the chain.

    return FLT_PREOP_SUCCESS_WITH_CALLBACK;
}



VOID
CryptMiniOperationStatusCallback (
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ PFLT_IO_PARAMETER_BLOCK ParameterSnapshot,
    _In_ NTSTATUS OperationStatus,
    _In_ PVOID RequesterContext
    )
/*++

Routine Description:

    This routine is called when the given operation returns from the call
    to IoCallDriver.  This is useful for operations where STATUS_PENDING
    means the operation was successfully queued.  This is useful for OpLocks
    and directory change notification operations.

    This callback is called in the context of the originating thread and will
    never be called at DPC level.  The file object has been correctly
    referenced so that you can access it.  It will be automatically
    dereferenced upon return.

    This is non-pageable because it could be called on the paging path

Arguments:

    FltObjects - Pointer to the FLT_RELATED_OBJECTS data structure containing
        opaque handles to this filter, instance, its associated volume and
        file object.

    RequesterContext - The context for the completion routine for this
        operation.

    OperationStatus -

Return Value:

    The return value is the status of the operation.

--*/
{
    UNREFERENCED_PARAMETER( FltObjects );

    LOG_PRINT( LOG_INFO,
                  ("[CryptMini]CryptMiniOperationStatusCallback: Entered\n") );

    LOG_PRINT( LOG_INFO,
                  ("[CryptMini]CryptMiniOperationStatusCallback: Status=%08x ctx=%p IrpMj=%02x.%02x \"%s\"\n",
                   OperationStatus,
                   RequesterContext,
                   ParameterSnapshot->MajorFunction,
                   ParameterSnapshot->MinorFunction,
                   FltGetIrpName(ParameterSnapshot->MajorFunction)) );
}


FLT_POSTOP_CALLBACK_STATUS
CryptMiniPostOperation (
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_opt_ PVOID CompletionContext,
    _In_ FLT_POST_OPERATION_FLAGS Flags
    )
/*++

Routine Description:

    This routine is the post-operation completion routine for this
    miniFilter.

    This is non-pageable because it may be called at DPC level.

Arguments:

    Data - Pointer to the filter callbackData that is passed to us.

    FltObjects - Pointer to the FLT_RELATED_OBJECTS data structure containing
        opaque handles to this filter, instance, its associated volume and
        file object.

    CompletionContext - The completion context set in the pre-operation routine.

    Flags - Denotes whether the completion is successful or is being drained.

Return Value:

    The return value is the status of the operation.

--*/
{
    UNREFERENCED_PARAMETER( Data );
    UNREFERENCED_PARAMETER( FltObjects );
    UNREFERENCED_PARAMETER( CompletionContext );
    UNREFERENCED_PARAMETER( Flags );

    LOG_PRINT( LOG_INFO,
                  ("[CryptMini]CryptMiniPostOperation: Entered\n") );

    return FLT_POSTOP_FINISHED_PROCESSING;
}


FLT_PREOP_CALLBACK_STATUS
CryptMiniPreOperationNoPostOperation (
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _Flt_CompletionContext_Outptr_ PVOID *CompletionContext
    )
/*++

Routine Description:

    This routine is a pre-operation dispatch routine for this miniFilter.

    This is non-pageable because it could be called on the paging path

Arguments:

    Data - Pointer to the filter callbackData that is passed to us.

    FltObjects - Pointer to the FLT_RELATED_OBJECTS data structure containing
        opaque handles to this filter, instance, its associated volume and
        file object.

    CompletionContext - The context for the completion routine for this
        operation.

Return Value:

    The return value is the status of the operation.

--*/
{
    UNREFERENCED_PARAMETER( Data );
    UNREFERENCED_PARAMETER( FltObjects );
    UNREFERENCED_PARAMETER( CompletionContext );

    LOG_PRINT( LOG_INFO,
                  ("[CryptMini]CryptMiniPreOperationNoPostOperation: Entered\n") );

    // This template code does not do anything with the callbackData, but
    // rather returns FLT_PREOP_SUCCESS_NO_CALLBACK.
    // This passes the request down to the next miniFilter in the chain.

    return FLT_PREOP_SUCCESS_NO_CALLBACK;
}


BOOLEAN
CryptMiniDoRequestOperationStatus(
    _In_ PFLT_CALLBACK_DATA Data
    )
/*++

Routine Description:

    This identifies those operations we want the operation status for.  These
    are typically operations that return STATUS_PENDING as a normal completion
    status.

Arguments:

Return Value:

    TRUE - If we want the operation status
    FALSE - If we don't

--*/
{
    PFLT_IO_PARAMETER_BLOCK iopb = Data->Iopb;

    //
    //  return boolean state based on which operations we are interested in
    //

    return (BOOLEAN)

            //
            //  Check for oplock operations
            //

             (((iopb->MajorFunction == IRP_MJ_FILE_SYSTEM_CONTROL) &&
               ((iopb->Parameters.FileSystemControl.Common.FsControlCode == FSCTL_REQUEST_FILTER_OPLOCK)  ||
                (iopb->Parameters.FileSystemControl.Common.FsControlCode == FSCTL_REQUEST_BATCH_OPLOCK)   ||
                (iopb->Parameters.FileSystemControl.Common.FsControlCode == FSCTL_REQUEST_OPLOCK_LEVEL_1) ||
                (iopb->Parameters.FileSystemControl.Common.FsControlCode == FSCTL_REQUEST_OPLOCK_LEVEL_2)))

              ||

              //
              //    Check for directy change notification
              //

              ((iopb->MajorFunction == IRP_MJ_DIRECTORY_CONTROL) &&
               (iopb->MinorFunction == IRP_MN_NOTIFY_CHANGE_DIRECTORY))
             );
}
