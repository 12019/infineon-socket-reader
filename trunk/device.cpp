
#include "internal.h"
#include "VirtualSCReader.h"
#include "queue.h"
#include "device.h"
#include "driver.h"
#include <stdio.h>
#include <winscard.h>
#include "memory.h"
#include <Sddl.h>

#include <winbase.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#define APDU_HEADER_LEN        					5
#define APDU_HEADER_P3_OFFSET   				4

#define INFINEON_CTRL_PORT							20074
#define INFINEON_DATA_PORT         			20073

#define INFINEON_CTRL_CMD_NULL 					0x30
#define INFINEON_CTRL_CMD_STOP      		0x31
#define INFINEON_CTRL_CMD_COLD_RST			0x32
#define INFINEON_CTRL_CMD_FACTORY_RST		0x34
#define INFINEON_CTRL_CMD_WARM_RST			0x38

HRESULT
CMyDevice::CreateInstance(
    __in IWDFDriver *FxDriver,
    __in IWDFDeviceInitialize * FxDeviceInit
    )
/*++
 
  Routine Description:

    This method creates and initializs an instance of the virtual smart card reader driver's 
    device callback object.

  Arguments:

    FxDeviceInit - the settings for the device.

    Device - a location to store the referenced pointer to the device object.

  Return Value:

    Status

--*/
{
	inFunc
    CComObject<CMyDevice>* device = NULL;
    HRESULT hr;

    //
    // Allocate a new instance of the device class.
    //
	hr = CComObject<CMyDevice>::CreateInstance(&device);

    if (device==NULL)
    {
        return E_OUTOFMEMORY;
    }

    //
    // Initialize the instance.
    //
	device->AddRef();
    FxDeviceInit->SetLockingConstraint(WdfDeviceLevel);

    CComPtr<IUnknown> spCallback;
    hr = device->QueryInterface(IID_IUnknown, (void**)&spCallback);

    CComPtr<IWDFDevice> spIWDFDevice;
    if (SUCCEEDED(hr))
    {
        hr = FxDriver->CreateDevice(FxDeviceInit, spCallback, &spIWDFDevice);
    }

	if (spIWDFDevice->CreateDeviceInterface(&SmartCardReaderGuid,NULL)!=0)
		OutputDebugString(L"CreateDeviceInterface Failed");

	SAFE_RELEASE(device);

    return hr;
}

void CMyDevice::IoSmartCardIsPresent(IWDFIoRequest* pRequest,SIZE_T inBufSize,SIZE_T outBufSize) {
	UNREFERENCED_PARAMETER(inBufSize);
	UNREFERENCED_PARAMETER(outBufSize);
	OutputDebugString(L"[IPRE]IOCTL_SMARTCARD_IS_PRESENT");
	BYTE ATR[100];
	DWORD ATRSize;
	if (QueryATR(ATR,&ATRSize))
		// there's a smart card present, so complete the request
		pRequest->CompleteWithInformation(STATUS_SUCCESS, 0);
	else {
		// there's no smart card present, so leave the request pending; it will be completed later
		waitInsertIpr=pRequest;
		IRequestCallbackCancel *callback;
		QueryInterface(__uuidof(IRequestCallbackCancel),(void**)&callback);
		pRequest->MarkCancelable(callback);
		callback->Release();
	}
}

void CMyDevice::IoSmartCardGetState(IWDFIoRequest* pRequest,SIZE_T inBufSize,SIZE_T outBufSize) {
	UNREFERENCED_PARAMETER(inBufSize);
	UNREFERENCED_PARAMETER(outBufSize);
	OutputDebugString(L"[GSTA]IOCTL_SMARTCARD_GET_STATE");
	BYTE ATR[100];
	DWORD ATRSize;
	if (!QueryATR(ATR,&ATRSize)) {
		OutputDebugString(L"[GSTA]SCARD_ABSENT");
		setInt(pRequest,SCARD_ABSENT);
	}
	else {
		OutputDebugString(L"[GSTA]SCARD_SPECIFIC");
		setInt(pRequest,SCARD_SPECIFIC);
	}
}
void CMyDevice::IoSmartCardIsAbsent(IWDFIoRequest* pRequest,SIZE_T inBufSize,SIZE_T outBufSize) {
	UNREFERENCED_PARAMETER(inBufSize);
	UNREFERENCED_PARAMETER(outBufSize);
	OutputDebugString(L"[IABS]IOCTL_SMARTCARD_IS_ABSENT");
	BYTE ATR[100];
	DWORD ATRSize;
	if (!QueryATR(ATR,&ATRSize))
		// there's no smart card present, so complete the request
		pRequest->CompleteWithInformation(STATUS_SUCCESS, 0);
	else {
		// there's a smart card present, so leave the request pending; it will be completed later
		waitRemoveIpr=pRequest;
		IRequestCallbackCancel *callback;

		QueryInterface(__uuidof(IRequestCallbackCancel),(void**)&callback);
		pRequest->MarkCancelable(callback);
		callback->Release();
	}
}

void CMyDevice::IoSmartCardPower(IWDFIoRequest* pRequest,SIZE_T inBufSize,SIZE_T outBufSize) {
	UNREFERENCED_PARAMETER(inBufSize);
	UNREFERENCED_PARAMETER(outBufSize);
	OutputDebugString(L"[POWR]IOCTL_SMARTCARD_POWER");
	DWORD code=getInt(pRequest);
	if (code==SCARD_COLD_RESET) {
		OutputDebugString(L"[POWR]SCARD_COLD_RESET");
	}
	else if (code==SCARD_WARM_RESET) {
		OutputDebugString(L"[POWR]SCARD_WARM_RESET");
	}
	else if (code==SCARD_POWER_DOWN) {
		OutputDebugString(L"[POWR]SCARD_POWER_DOWN");
	}
	if (code==SCARD_COLD_RESET || code==SCARD_WARM_RESET) {
		BYTE ATR[100];
		DWORD ATRsize;
		if (!QueryATR(ATR,&ATRsize,true))
		{
			pRequest->CompleteWithInformation(STATUS_NO_MEDIA, 0);					
			return;
		}
		setBuffer(pRequest,ATR,ATRsize);
	}
	else {
		pRequest->CompleteWithInformation(STATUS_SUCCESS, 0);
	}
}

void CMyDevice::IoSmartCardSetAttribute(IWDFIoRequest* pRequest,SIZE_T inBufSize,SIZE_T outBufSize) {
	UNREFERENCED_PARAMETER(inBufSize);
	UNREFERENCED_PARAMETER(outBufSize);
	OutputDebugString(L"[SATT]IOCTL_SMARTCARD_SET_ATTRIBUTE");
	
	IWDFMemory *inmem=NULL;
	pRequest->GetInputMemory(&inmem);
	
	SIZE_T size;
	BYTE *data=(BYTE *)inmem->GetDataBuffer(&size);

	DWORD minCode=*(DWORD*)(data);
	bool handled=false;
	if (minCode==SCARD_ATTR_DEVICE_IN_USE) {
		OutputDebugString(L"[SATT]SCARD_ATTR_DEVICE_IN_USE");
		pRequest->CompleteWithInformation(STATUS_SUCCESS, 0);
		handled=true;
	}
	inmem->Release();

	if (!handled) {
		wchar_t log[300];
		swprintf(log,L"[SATT]ERROR_NOT_SUPPORTED:%08X",minCode);
		OutputDebugString(log);
		pRequest->CompleteWithInformation(HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED), 0);
	}
}

void CMyDevice::IoSmartCardTransmit(IWDFIoRequest* pRequest,SIZE_T inBufSize,SIZE_T outBufSize) {
	UNREFERENCED_PARAMETER(inBufSize);
	UNREFERENCED_PARAMETER(outBufSize);
	OutputDebugString(L"[TRSM]IOCTL_SMARTCARD_TRANSMIT");
	BYTE APDU[1000];
	int APDUSize;
	getBuffer(pRequest,APDU,&APDUSize);
	if (((SCARD_IO_REQUEST*)APDU)->dwProtocol!=SCARD_PROTOCOL_T0) {
        pRequest->CompleteWithInformation(STATUS_INVALID_DEVICE_STATE, 0);
		return;
	}
	BYTE Resp[1000];
	int RespSize;
	if (!QueryTransmit(APDU+sizeof(SCARD_IO_REQUEST),APDUSize-sizeof(SCARD_IO_REQUEST),Resp+sizeof(SCARD_IO_REQUEST),&RespSize))
	{
		pRequest->CompleteWithInformation(STATUS_NO_MEDIA, 0);					
		return;
	}
	((SCARD_IO_REQUEST*)Resp)->cbPciLength=sizeof(SCARD_IO_REQUEST);
	((SCARD_IO_REQUEST*)Resp)->dwProtocol=SCARD_PROTOCOL_T0;
	setBuffer(pRequest,Resp,RespSize+sizeof(SCARD_IO_REQUEST));
}

void CMyDevice::IoSmartCardGetAttribute(IWDFIoRequest* pRequest,SIZE_T inBufSize,SIZE_T outBufSize) {
	UNREFERENCED_PARAMETER(inBufSize);
	wchar_t log[300];
	DWORD code=getInt(pRequest);
	swprintf(log,L"[GATT]  - code %0X",code);
	OutputDebugString(log);

	switch(code) {
		case SCARD_ATTR_CHARACTERISTICS:
			// 0x00000000 No special characteristics
			OutputDebugString(L"[GATT]SCARD_ATTR_CHARACTERISTICS");
			setInt(pRequest,0);
			return;
		case SCARD_ATTR_VENDOR_NAME:
			OutputDebugString(L"[GATT]SCARD_ATTR_VENDOR_NAME");
			setString(pRequest,"Infineon",(int)outBufSize);
			return;
		case SCARD_ATTR_VENDOR_IFD_TYPE:
			OutputDebugString(L"[GATT]SCARD_ATTR_VENDOR_IFD_TYPE");
			setString(pRequest,"VIRTUAL_CARD_READER",(int)outBufSize);
			return;
		case SCARD_ATTR_DEVICE_UNIT:
			OutputDebugString(L"[GATT]SCARD_ATTR_DEVICE_UNIT");
			setInt(pRequest,0);
			return;
		case SCARD_ATTR_ATR_STRING:
			OutputDebugString(L"[GATT]SCARD_ATTR_ATR_STRING");
			BYTE ATR[100];
			DWORD ATRsize;
			if (!QueryATR(ATR,&ATRsize))
			{
				pRequest->CompleteWithInformation(STATUS_NO_MEDIA, 0);					
				return;
			}
			setBuffer(pRequest,ATR,ATRsize);
			return;
		case SCARD_ATTR_CURRENT_PROTOCOL_TYPE:
			OutputDebugString(L"[GATT]SCARD_ATTR_CURRENT_PROTOCOL_TYPE");
			setInt(pRequest,SCARD_PROTOCOL_T0); // T=0
			return;
		default:
			swprintf(log,L"[GATT]ERROR_NOT_SUPPORTED:%08X",code);
			OutputDebugString(log);
	        pRequest->CompleteWithInformation(HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED), 0);
	}
}

void CMyDevice::ProcessIoControl(__in IWDFIoQueue*     pQueue,
                                    __in IWDFIoRequest*   pRequest,
                                    __in ULONG            ControlCode,
                                         SIZE_T           inBufSize,
                                         SIZE_T           outBufSize)
{
	inFunc
	UNREFERENCED_PARAMETER(pQueue);
	wchar_t log[300];
	swprintf(log,L"[IOCT]IOCTL %08X - In %i Out %i",ControlCode,inBufSize,outBufSize);
	OutputDebugString(log);

	if (ControlCode==IOCTL_SMARTCARD_GET_ATTRIBUTE) {
		IoSmartCardGetAttribute(pRequest,inBufSize,outBufSize);
		return;
	}
	else if (ControlCode==IOCTL_SMARTCARD_IS_PRESENT) {
		IoSmartCardIsPresent(pRequest,inBufSize,outBufSize);
		return;
	}
	else if (ControlCode==IOCTL_SMARTCARD_GET_STATE) {
		IoSmartCardGetState(pRequest,inBufSize,outBufSize);
		return;
	}
	else if (ControlCode==IOCTL_SMARTCARD_IS_ABSENT) {
		IoSmartCardIsAbsent(pRequest,inBufSize,outBufSize);
		return;
	}
	else if (ControlCode==IOCTL_SMARTCARD_POWER) {
		IoSmartCardPower(pRequest,inBufSize,outBufSize);
		return;
	}	
	else if (ControlCode==IOCTL_SMARTCARD_SET_ATTRIBUTE) {
		IoSmartCardSetAttribute(pRequest,inBufSize,outBufSize);
		return;
	}
	else if (ControlCode==IOCTL_SMARTCARD_TRANSMIT) {
		IoSmartCardTransmit(pRequest,inBufSize,outBufSize);
		return;
	}
	swprintf(log,L"[IOCT]ERROR_NOT_SUPPORTED:%08X",ControlCode);
	OutputDebugString(log);
    pRequest->CompleteWithInformation(HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED), 0);

    return;
}
BOOL CreateMyDACL(SECURITY_ATTRIBUTES * pSA)
{
     TCHAR * szSD = TEXT("D:")       // Discretionary ACL
        TEXT("(D;OICI;GA;;;BG)")     // Deny access to 
                                     // built-in guests
        TEXT("(D;OICI;GA;;;AN)")     // Deny access to 
                                     // anonymous logon	
        TEXT("(A;OICI;GRGWGX;;;AU)") // Allow 
                                     // read/write/execute 
                                     // to authenticated 
                                     // users
        TEXT("(A;OICI;GA;;;BA)");    // Allow full control 
                                     // to administrators

    if (NULL == pSA)
        return FALSE;

     return ConvertStringSecurityDescriptorToSecurityDescriptor(
                szSD,
                SDDL_REVISION_1,
                &(pSA->lpSecurityDescriptor),
                NULL);
}

bool CMyDevice::QueryTransmit(BYTE *APDU,int APDUlen,BYTE *Resp,int *Resplen) {
	if (ctrlSocket==INVALID_SOCKET)
		return false;
	if (send(dataSocket,(const char *)APDU,APDU_HEADER_LEN,0)==-1) {
		ResetSocket();
		return false;
	}
	(*Resplen)=0;
recv_procedure_byte:
	if (recv(dataSocket,(char *)Resp,1,0)==-1) {
		ResetSocket();
		return false;
	}
	if (Resp[0]==0x60) { //NULL
		goto recv_procedure_byte;
	}
	else if ((Resp[0]&0xF0)==0x60 || (Resp[0]&0xF0)==0x90) { //SW1
		if (recv(dataSocket,(char *)(Resp+1),1,0)==-1) {
			ResetSocket();
			return false;
		}
		(*Resplen)+=2;
		return true;
	}
	else if (Resp[0]==APDU[1]) { //ACK INS, not support INS^0xFF yet
		if (APDUlen>APDU_HEADER_LEN) {
			if ((APDUlen-APDU_HEADER_LEN)!=APDU[APDU_HEADER_P3_OFFSET]
				|| send(dataSocket,(const char *)(APDU+APDU_HEADER_LEN),APDUlen-APDU_HEADER_LEN,0)==-1) {
				ResetSocket();
				return false;
			}
		} else {
			if (recv(dataSocket,(char *)Resp,APDUlen-APDU_HEADER_LEN,0)==-1) {
				ResetSocket();
				return false;
			}
			Resp+=(APDUlen-APDU_HEADER_LEN);
			(*Resplen)+=(APDUlen-APDU_HEADER_LEN);
		}
		goto recv_procedure_byte;
	}
	// shouldn't reached here
	return false;
}

bool CMyDevice::QueryATR(BYTE *ATR,DWORD *ATRsize,bool reset) {
	if (ctrlSocket==INVALID_SOCKET) {
reset_connection:
    SOCKET _ctrlSocket=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
		struct sockaddr_in my_addr;
		memset(&my_addr,0,sizeof(my_addr));
		my_addr.sin_family = AF_INET;
		my_addr.sin_port = htons(INFINEON_CTRL_PORT);
		my_addr.sin_addr.s_addr = INADDR_ANY;
		int nret=connect(_ctrlSocket, (struct sockaddr *)&my_addr,sizeof(struct sockaddr));

		if (nret==SOCKET_ERROR) {
				closesocket(_ctrlSocket);
				return false;
		}

    SOCKET _dataSocket=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
		memset(&my_addr,0,sizeof(my_addr));
		my_addr.sin_family = AF_INET;
		my_addr.sin_port = htons(INFINEON_DATA_PORT);
		my_addr.sin_addr.s_addr = INADDR_ANY;
		nret=connect(_dataSocket, (struct sockaddr *)&my_addr,sizeof(struct sockaddr));
		if (nret==SOCKET_ERROR) {
				closesocket(_dataSocket);
				return false;
		}

		ctrlSocket=_ctrlSocket;
		dataSocket=_dataSocket;

		if ((atrLen=recv(dataSocket,(char *)savedATR,MAX_ATR_LENGTH,0))==-1) {
			return false;
		}
	} else {
		BYTE ctrl=INFINEON_CTRL_CMD_NULL;
		if (send(ctrlSocket,(const char *)&ctrl,sizeof(ctrl),0)==-1) { //detect
			ResetSocket();
			atrLen=0;
			return false;
		}
	}

	if (reset==true) {
		BYTE rst=INFINEON_CTRL_CMD_COLD_RST;
		if (send(ctrlSocket,(const char *)&rst,sizeof(rst),0)==-1) { //send reset command
			ResetSocket();
			goto reset_connection;
		}
		if ((atrLen=recv(dataSocket,(char *)savedATR,MAX_ATR_LENGTH,0))==-1) {
			return false;
		}
		//PPS procedure
/*		BYTE PPSCmd[]={0xFF,0x00,0xFF,0xFF};
		if ((send(dataSocket,(const char *)PPSCmd,sizeof(PPSCmd),0)==-1)
				|| (recv(dataSocket,(char *)PPSCmd,sizeof(PPSCmd),0)==-1)) {
			ResetSocket();
			atrLen=0;
			return false;
		}*/
	} else {
		if (atrLen==0) return false;
	}

	memcpy(ATR,savedATR,atrLen);
	(*ATRsize)=atrLen;
	return true;
}

/////////////////////////////////////////////////////////////////////////
//
// CMyDevice::OnPrepareHardware
//
//  Called by UMDF to prepare the hardware for use. 
//
// Parameters:
//      pWdfDevice - pointer to an IWDFDevice object representing the
//      device
//
// Return Values:
//      S_OK: success
//
/////////////////////////////////////////////////////////////////////////
HRESULT CMyDevice::OnPrepareHardware(
        __in IWDFDevice* pWdfDevice
        )
{
	inFunc
    // Store the IWDFDevice pointer
    m_pWdfDevice = pWdfDevice;

    // Configure the default IO Queue
    HRESULT hr = CMyQueue::CreateInstance(m_pWdfDevice, this);

//	DWORD socketThreadID;
//	CreateThread(NULL,0,(LPTHREAD_START_ROUTINE)socketServerFunc,this,0,&socketThreadID);
    return hr;
}


/////////////////////////////////////////////////////////////////////////
//
// CMyDevice::OnReleaseHardware
//
// Called by WUDF to uninitialize the hardware.
//
// Parameters:
//      pWdfDevice - pointer to an IWDFDevice object for the device
//
// Return Values:
//      S_OK:
//
/////////////////////////////////////////////////////////////////////////
HRESULT CMyDevice::OnReleaseHardware(
        __in IWDFDevice* pWdfDevice
        )
{
	inFunc
    UNREFERENCED_PARAMETER(pWdfDevice);
    
    // Release the IWDFDevice handle, if it matches
    if (pWdfDevice == m_pWdfDevice.p)
    {
        m_pWdfDevice.Release();
    }

    return S_OK;
}


HRESULT CMyDevice::OnD0Entry(IN IWDFDevice*  pWdfDevice,IN WDF_POWER_DEVICE_STATE  previousState) {
	UNREFERENCED_PARAMETER(pWdfDevice);
	UNREFERENCED_PARAMETER(previousState);
	OutputDebugString(L"OnD0Entry");
	return S_OK;
}
HRESULT CMyDevice::OnD0Exit(IN IWDFDevice*  pWdfDevice,IN WDF_POWER_DEVICE_STATE  newState) {
	UNREFERENCED_PARAMETER(pWdfDevice);
	UNREFERENCED_PARAMETER(newState);
	OutputDebugString(L"OnD0Exit");
	return S_OK;
}

void CMyDevice::shutDown(){
	if (waitRemoveIpr!=NULL) {
		if (waitRemoveIpr->UnmarkCancelable()==S_OK)
			waitRemoveIpr->Complete(HRESULT_FROM_WIN32(ERROR_CANCELLED));
		waitRemoveIpr=NULL;
	}
	if (waitInsertIpr!=NULL) {
		if (waitInsertIpr->UnmarkCancelable()==S_OK)
			waitInsertIpr->Complete(HRESULT_FROM_WIN32(ERROR_CANCELLED));
		waitInsertIpr=NULL;
	}
}

void CMyDevice::ResetSocket() {
		closesocket(ctrlSocket);
		closesocket(dataSocket);
		ctrlSocket=INVALID_SOCKET;
		dataSocket=INVALID_SOCKET;
}

HRESULT CMyDevice::OnQueryRemove(IN IWDFDevice*  pWdfDevice) {
	UNREFERENCED_PARAMETER(pWdfDevice);
	OutputDebugString(L"OnQueryRemove");
	shutDown();
	return S_OK;
}
HRESULT CMyDevice::OnQueryStop(IN IWDFDevice*  pWdfDevice) {
	UNREFERENCED_PARAMETER(pWdfDevice);
	OutputDebugString(L"OnQueryStop");
	shutDown();
	return S_OK;
}
void CMyDevice::OnSurpriseRemoval(IN IWDFDevice*  pWdfDevice) {
	UNREFERENCED_PARAMETER(pWdfDevice);
	OutputDebugString(L"OnSurpriseRemoval");
	shutDown();
}

STDMETHODIMP_ (void) CMyDevice::OnCancel(IN IWDFIoRequest*  pWdfRequest) {
	OutputDebugString(L"OnCancel Request");
	if (pWdfRequest==waitRemoveIpr) {
		OutputDebugString(L"Cancel Remove");
		waitRemoveIpr=NULL;
	}
	else if (pWdfRequest==waitInsertIpr) {
		OutputDebugString(L"Cancel Insert");
		waitInsertIpr=NULL;
	}
	pWdfRequest->CompleteWithInformation(HRESULT_FROM_WIN32(ERROR_CANCELLED), 0);
}
