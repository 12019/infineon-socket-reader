#pragma once
#include "internal.h"


bool getBuffer(IWDFIoRequest* pRequest,BYTE *buffer,int *bufferLen);
void setString(IWDFIoRequest* pRequest,char *result,int outSize);
void setBuffer(IWDFIoRequest* pRequest,BYTE *result,int inSize);
void setInt(IWDFIoRequest* pRequest,DWORD result);
DWORD getInt(IWDFIoRequest* pRequest);
