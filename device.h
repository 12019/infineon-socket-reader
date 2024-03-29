

#pragma once

#define MAX_ATR_LENGTH				32

//
// Class for the iotrace driver.
//

class ATL_NO_VTABLE CMyDevice :
    public CComObjectRootEx<CComMultiThreadModel>,
    public IPnpCallbackHardware,
	public IPnpCallback,
	public IRequestCallbackCancel
{
public:
	~CMyDevice() {}

    DECLARE_NOT_AGGREGATABLE(CMyDevice)

    BEGIN_COM_MAP(CMyDevice)
        COM_INTERFACE_ENTRY(IPnpCallbackHardware)
        COM_INTERFACE_ENTRY(IPnpCallback)
		COM_INTERFACE_ENTRY(IRequestCallbackCancel)
    END_COM_MAP()

    CMyDevice(VOID)
    {
        m_pWdfDevice = NULL;
        waitRemoveIpr=NULL;
        waitInsertIpr=NULL;
        dataSocket = INVALID_SOCKET;
        ctrlSocket = INVALID_SOCKET;
        atrLen = 0;
    }

	//
// Private data members.
//
private:
        
        CComPtr<IWDFDevice>     m_pWdfDevice;
	CComPtr<IWDFIoRequest> waitRemoveIpr,waitInsertIpr;
	SOCKET dataSocket;
	SOCKET ctrlSocket;
	BYTE savedATR[MAX_ATR_LENGTH];
	int atrLen;
//
// Private methods.
//

private:


	HRESULT ConfigureQueue();

	//
// Public methods
//
public:

    //
    // The factory method used to create an instance of this driver.
    //
    
    static
    HRESULT CreateInstance(
        __in IWDFDriver *FxDriver,
        __in IWDFDeviceInitialize *FxDeviceInit
        );

//
// COM methods
//
public:

    //
    // IUnknown methods.
    //

	void  ProcessIoControl(__in IWDFIoQueue*     pQueue,
                                    __in IWDFIoRequest*   pRequest,
                                    __in ULONG            ControlCode,
                                         SIZE_T           InputBufferSizeInBytes,
                                         SIZE_T           OutputBufferSizeInBytes);

    STDMETHOD_ (HRESULT, OnPrepareHardware)(__in IWDFDevice* pWdfDevice);
    STDMETHOD_ (HRESULT, OnReleaseHardware)(__in IWDFDevice* pWdfDevice);
    STDMETHOD_ (HRESULT, OnD0Entry)(IN IWDFDevice*  pWdfDevice,IN WDF_POWER_DEVICE_STATE  previousState);
    STDMETHOD_ (HRESULT, OnD0Exit )(IN IWDFDevice*  pWdfDevice,IN WDF_POWER_DEVICE_STATE  newState);
    STDMETHOD_ (HRESULT, OnQueryRemove )(IN IWDFDevice*  pWdfDevice);
    STDMETHOD_ (HRESULT, OnQueryStop)(IN IWDFDevice*  pWdfDevice);
    STDMETHOD_ (void, OnSurpriseRemoval)(IN IWDFDevice*  pWdfDevice);
    STDMETHOD_ (void, OnCancel)(IN IWDFIoRequest*  pWdfRequest);

	void IoSmartCardGetAttribute(IWDFIoRequest* pRequest,SIZE_T inBufSize,SIZE_T outBufSize);
	void IoSmartCardIsPresent(IWDFIoRequest* pRequest,SIZE_T inBufSize,SIZE_T outBufSize);
	void IoSmartCardGetState(IWDFIoRequest* pRequest,SIZE_T inBufSize,SIZE_T outBufSize);
	void IoSmartCardIsAbsent(IWDFIoRequest* pRequest,SIZE_T inBufSize,SIZE_T outBufSize);
	void IoSmartCardPower(IWDFIoRequest* pRequest,SIZE_T inBufSize,SIZE_T outBufSize);
	void IoSmartCardSetAttribute(IWDFIoRequest* pRequest,SIZE_T inBufSize,SIZE_T outBufSize);
	void IoSmartCardTransmit(IWDFIoRequest* pRequest,SIZE_T inBufSize,SIZE_T outBufSize);

	bool QueryTransmit(BYTE *APDU,int APDUlen,BYTE *Resp,int *Resplen);
	bool QueryATR(BYTE *ATR,DWORD *ATRsize,bool reset=false);
	void shutDown();
	void ResetSocket();
};

