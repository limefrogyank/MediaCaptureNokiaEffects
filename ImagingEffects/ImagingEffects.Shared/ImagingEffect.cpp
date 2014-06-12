// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved.

#include "pch.h"
#include <wrl\module.h>
#include "ImagingEffect.h"
#include "VideoBufferLock.h"
#include "NativeBuffer.h"

//include use to acces IBuffer memory
#include <wrl.h>
#include <robuffer.h>
#include <collection.h>
#include <vector>
#include <map>
#include <sstream>		
#include <algorithm>  //for str.remove


#pragma comment(lib, "d2d1")

using namespace Microsoft::WRL;
using namespace concurrency;
using namespace Windows::Storage::Streams;
using namespace Nokia::Graphics::Imaging;
using namespace Windows::Foundation::Collections;
using namespace Platform::Collections;

ActivatableClass(CImagingEffect);


// Video FOURCC codes.
const DWORD FOURCC_YUY2 = '2YUY';
const DWORD FOURCC_UYVY = 'YVYU';
const DWORD FOURCC_NV12 = '21VN';

// Static array of media types (preferred and accepted).
const GUID g_MediaSubtypes[] =
{
	MFVideoFormat_NV12,
	MFVideoFormat_YUY2
};

DWORD GetImageSize(DWORD fcc, UINT32 width, UINT32 height);
LONG GetDefaultStride(IMFMediaType *pType);

template <typename T>
inline T clamp(const T &val, const T &minVal, const T &maxVal)
{
	return (val < minVal ? minVal : (val > maxVal ? maxVal : val));
}

//-------------------------------------------------------------------
// Functions to convert a YUV images to grayscale.
//
// In all cases, the same transformation is applied to the 8-bit
// chroma values, but the pixel layout in memory differs.
//
// The image conversion functions take the following parameters:
//
// mat               Transfomation matrix for chroma values.
// rcDest            Destination rectangle.
// pDest             Pointer to the destination buffer.
// lDestStride       Stride of the destination buffer, in bytes.
// pSrc              Pointer to the source buffer.
// lSrcStride        Stride of the source buffer, in bytes.
// dwWidthInPixels   Frame width in pixels.
// dwHeightInPixels  Frame height, in pixels.
//-------------------------------------------------------------------


Nokia::Graphics::Imaging::Bitmap^ AsBitmapYUY2(const unsigned char* source, unsigned int width, unsigned int height)
{
	int totalDimensionLength = width * height;

	int size = totalDimensionLength * 4; //YUY2 buffer will be returned from the camera.

	ComPtr<ImagingEffects::NativeBuffer> nativeBuffer;
	MakeAndInitialize<ImagingEffects::NativeBuffer>(&nativeBuffer, (byte *)source, size);
	auto iinspectable = (IInspectable *)reinterpret_cast<IInspectable *>(nativeBuffer.Get());
	IBuffer ^buffer = reinterpret_cast<IBuffer ^>(iinspectable);

	nativeBuffer = nullptr;

	return ref new Bitmap(Windows::Foundation::Size((float)width, (float)height), ColorMode::Yuv422_Y1UY2V, 2 * width, buffer);
}



Array<BYTE>^ MakeManagedArray(const BYTE* input, int len)
{
	Array<BYTE>^ result = ref new Array<BYTE>(len);
	for (int i = 0; i < len; i++)
	{
		result[i] = input[i];
	}
	return result;
}


byte* GetPointerToPixelData(Windows::Storage::Streams::IBuffer^ pixelBuffer, unsigned int *length = nullptr)
{
	if (length != nullptr)
	{
		*length = pixelBuffer->Length;
	}
	// Query the IBufferByteAccess interface.
	Microsoft::WRL::ComPtr< Windows::Storage::Streams::IBufferByteAccess> bufferByteAccess;
	reinterpret_cast<IInspectable*>(pixelBuffer)->QueryInterface(IID_PPV_ARGS(&bufferByteAccess));


	// Retrieve the buffer data.
	byte* pixels = nullptr;
	bufferByteAccess->Buffer(&pixels);
	return pixels;
}


template<typename T>
byte range(T v)
{
	return v<0 ? (byte)0 : v>255 ? (byte)255 : (byte)v + .5;
}

static BYTE clip(int i)
{
	if (i < 0)
		return 0;
	if (i > 255)
		return 255;

	return i;
}

Nokia::Graphics::Imaging::Bitmap^ AsBitmapNV12(const unsigned char* source, unsigned int width, unsigned int height)
{
	int totalDimensionLength = width * height;

	//Y buffer will be having a length of Width x Height
	int yBufferLength = totalDimensionLength;

	//UV Buffer will be Width/2 and Height/2 and each will take 2 bytes
	int UVLength = (int)((double)totalDimensionLength / 2);

	int size = yBufferLength + UVLength; //NV12 buffer will be returned from the camera.

	ComPtr<ImagingEffects::NativeBuffer> nativeBuffer;
	MakeAndInitialize<ImagingEffects::NativeBuffer>(&nativeBuffer, (byte *)source, yBufferLength);
	auto iinspectable = (IInspectable *)reinterpret_cast<IInspectable *>(nativeBuffer.Get());
	IBuffer ^bufferY = reinterpret_cast<IBuffer ^>(iinspectable);

	MakeAndInitialize<ImagingEffects::NativeBuffer>(&nativeBuffer, (byte *)source + yBufferLength, UVLength);
	iinspectable = (IInspectable *)reinterpret_cast<IInspectable *>(nativeBuffer.Get());
	IBuffer ^bufferUV = reinterpret_cast<IBuffer ^>(iinspectable);

	nativeBuffer = nullptr;

	Platform::Array<unsigned int, 1U>^ inputScanlines = ref new Platform::Array<unsigned int>(2);      // for NV12 2 planes Y and UV.
	Platform::Array<IBuffer^, 1U>^ inputBuffers = ref new Platform::Array<IBuffer^>(2);

	//setting the input Buffers according to NV12 format
	inputBuffers[0] = bufferY;
	inputBuffers[1] = bufferUV;

	inputScanlines[0] = (unsigned int)width; // YBuffer,  w items of 1 byte long
	inputScanlines[1] = (unsigned int)width; // UVBuffer, Each UV is 2 bytes long, and there are w/2 of them.

	return ref new Bitmap(Windows::Foundation::Size((float)width, (float)height), ColorMode::Yuv420Sp, inputScanlines, inputBuffers);
}

inline void ThrowIfFailed(HRESULT hr)
{
	if (FAILED(hr))
	{
		// Set a breakpoint on this line to catch Win32 API errors.
		throw Platform::Exception::CreateException(hr);
	}
}

unsigned char* FromIBuffer(Windows::Storage::Streams::IBuffer^ outputBuffer)
{
	// Com magic to retrieve the pointer to the pixel buffer.
	Object^ obj = outputBuffer;
	ComPtr<IInspectable> insp(reinterpret_cast<IInspectable*>(obj));
	ComPtr<IBufferByteAccess> bufferByteAccess;
	ThrowIfFailed(insp.As(&bufferByteAccess));
	//insp.As(&bufferByteAccess);
	unsigned char* pixels = nullptr;
	ThrowIfFailed(bufferByteAccess->Buffer(&pixels));

	return pixels;
}


// Convert YUY2 image.

void TransformImage_YUY2(
	const D2D_RECT_U &rcDest,
	_Inout_updates_(_Inexpressible_(lDestStride * dwHeightInPixels)) BYTE *pDest,
	_In_ LONG lDestStride,
	_In_reads_(_Inexpressible_(lSrcStride * dwHeightInPixels)) const BYTE *pSrc,
	_In_ LONG lSrcStride,
	_In_ DWORD dwWidthInPixels,
	_In_ DWORD dwHeightInPixels,
	IVector<IImageProvider^>^ providers)
{
	auto size = Windows::Foundation::Size(dwWidthInPixels, dwHeightInPixels);
	auto totalbytes = (int)dwHeightInPixels * (int)dwWidthInPixels * 2;  //each macropixel of 4 bytes creates 2 pixels (YUYV)

	Nokia::Graphics::Imaging::Bitmap^ m_BitmapToProcess = AsBitmapYUY2(pSrc, (unsigned int)size.Width, (unsigned int)size.Height);

	BitmapImageSource^ source = ref new BitmapImageSource(m_BitmapToProcess);
	auto first = providers->GetAt(0);
	((IImageConsumer^)first)->Source = source;

	auto last = providers->GetAt(providers->Size - 1);

	BitmapRenderer^ renderer = ref new BitmapRenderer(last, AsBitmapYUY2(pDest, (unsigned int)size.Width, (unsigned int)size.Height));

	auto renderOp = renderer->RenderAsync();
	auto renderTask = create_task(renderOp);

	renderTask.wait();
}

// Convert NV12 image

void TransformImage_NV12(
	const D2D_RECT_U &rcDest,
	_Inout_updates_(_Inexpressible_(2 * lDestStride * dwHeightInPixels)) BYTE *pDest,
	_In_ LONG lDestStride,
	_In_reads_(_Inexpressible_(2 * lSrcStride * dwHeightInPixels)) const BYTE *pSrc,
	_In_ LONG lSrcStride,
	_In_ DWORD dwWidthInPixels,
	_In_ DWORD dwHeightInPixels,
	IVector<IImageProvider^>^ providers)
{
	auto size = Windows::Foundation::Size(dwWidthInPixels, dwHeightInPixels);
	auto totalbytes = (int)dwHeightInPixels * (int)dwWidthInPixels * 3 / 2;

	Nokia::Graphics::Imaging::Bitmap^ m_BitmapToProcess = AsBitmapNV12(pSrc, (unsigned int)size.Width, (unsigned int)size.Height);

	BitmapImageSource^ source = ref new BitmapImageSource(m_BitmapToProcess);
	auto first = providers->GetAt(0);
	((IImageConsumer^)first)->Source = source;

	auto last = providers->GetAt(providers->Size - 1);

	//BitmapRenderer^ renderer = ref new BitmapRenderer(last, ColorMode::Yuv420Sp);
	BitmapRenderer^ renderer = ref new BitmapRenderer(last, AsBitmapNV12(pDest, (unsigned int)size.Width, (unsigned int)size.Height));

	auto renderOp = renderer->RenderAsync();
	auto renderTask = create_task(renderOp);

	renderTask.wait();
}

CImagingEffect::CImagingEffect()
	: m_pTransformFn(nullptr)
	, m_imageWidthInPixels(0)
	, m_imageHeightInPixels(0)
	, m_cbImageSize(0)
	, m_rcDest(D2D1::RectU())
	, m_fStreamingInitialized(false)
{
}

CImagingEffect::~CImagingEffect()
{
}

// Initialize the instance.
STDMETHODIMP CImagingEffect::RuntimeClassInitialize()
{
	// Create the attribute store.
	return MFCreateAttributes(&m_spAttributes, 3);
}

// IMediaExtension methods

//-------------------------------------------------------------------
// SetProperties
// Sets the configuration of the effect
//-------------------------------------------------------------------
HRESULT CImagingEffect::SetProperties(ABI::Windows::Foundation::Collections::IPropertySet *pConfiguration)
{
	HRESULT hr = S_OK;

	try
	{
		IPropertySet^ properties = reinterpret_cast<IPropertySet^>(pConfiguration);
		m_imageProviders = safe_cast<IVector<IImageProvider^>^>(properties->Lookup(L"IImageProviders"));
	}
	catch (Exception ^exc)
	{
		hr = exc->HResult;
	}
	return hr;
}

// IMFTransform methods. Refer to the Media Foundation SDK documentation for details.

//-------------------------------------------------------------------
// GetStreamLimits
// Returns the minimum and maximum number of streams.
//-------------------------------------------------------------------

HRESULT CImagingEffect::GetStreamLimits(
	DWORD   *pdwInputMinimum,
	DWORD   *pdwInputMaximum,
	DWORD   *pdwOutputMinimum,
	DWORD   *pdwOutputMaximum
	)
{
	if ((pdwInputMinimum == nullptr) ||
		(pdwInputMaximum == nullptr) ||
		(pdwOutputMinimum == nullptr) ||
		(pdwOutputMaximum == nullptr))
	{
		return E_POINTER;
	}

	// This MFT has a fixed number of streams.
	*pdwInputMinimum = 1;
	*pdwInputMaximum = 1;
	*pdwOutputMinimum = 1;
	*pdwOutputMaximum = 1;
	return S_OK;
}


//-------------------------------------------------------------------
// GetStreamCount
// Returns the actual number of streams.
//-------------------------------------------------------------------

HRESULT CImagingEffect::GetStreamCount(
	DWORD   *pcInputStreams,
	DWORD   *pcOutputStreams
	)
{
	if ((pcInputStreams == nullptr) || (pcOutputStreams == nullptr))

	{
		return E_POINTER;
	}

	// This MFT has a fixed number of streams.
	*pcInputStreams = 1;
	*pcOutputStreams = 1;
	return S_OK;
}



//-------------------------------------------------------------------
// GetStreamIDs
// Returns stream IDs for the input and output streams.
//-------------------------------------------------------------------

HRESULT CImagingEffect::GetStreamIDs(
	DWORD   dwInputIDArraySize,
	DWORD   *pdwInputIDs,
	DWORD   dwOutputIDArraySize,
	DWORD   *pdwOutputIDs
	)
{
	// It is not required to implement this method if the MFT has a fixed number of
	// streams AND the stream IDs are numbered sequentially from zero (that is, the
	// stream IDs match the stream indexes).

	// In that case, it is OK to return E_NOTIMPL.
	return E_NOTIMPL;
}


//-------------------------------------------------------------------
// GetInputStreamInfo
// Returns information about an input stream.
//-------------------------------------------------------------------

HRESULT CImagingEffect::GetInputStreamInfo(
	DWORD                     dwInputStreamID,
	MFT_INPUT_STREAM_INFO *   pStreamInfo
	)
{
	if (pStreamInfo == nullptr)
	{
		return E_POINTER;
	}

	AutoLock lock(m_critSec);

	if (!IsValidInputStream(dwInputStreamID))
	{
		return MF_E_INVALIDSTREAMNUMBER;
	}

	// NOTE: This method should succeed even when there is no media type on the
	//       stream. If there is no media type, we only need to fill in the dwFlags
	//       member of MFT_INPUT_STREAM_INFO. The other members depend on having a
	//       a valid media type.

	pStreamInfo->hnsMaxLatency = 0;
	pStreamInfo->dwFlags = MFT_INPUT_STREAM_WHOLE_SAMPLES | MFT_INPUT_STREAM_SINGLE_SAMPLE_PER_BUFFER;

	if (m_spInputType == nullptr)
	{
		pStreamInfo->cbSize = 0;
	}
	else
	{
		pStreamInfo->cbSize = m_cbImageSize;
	}

	pStreamInfo->cbMaxLookahead = 0;
	pStreamInfo->cbAlignment = 0;

	return S_OK;
}

//-------------------------------------------------------------------
// GetOutputStreamInfo
// Returns information about an output stream.
//-------------------------------------------------------------------

HRESULT CImagingEffect::GetOutputStreamInfo(
	DWORD                     dwOutputStreamID,
	MFT_OUTPUT_STREAM_INFO *  pStreamInfo
	)
{
	if (pStreamInfo == nullptr)
	{
		return E_POINTER;
	}

	AutoLock lock(m_critSec);

	if (!IsValidOutputStream(dwOutputStreamID))
	{
		return MF_E_INVALIDSTREAMNUMBER;
	}

	// NOTE: This method should succeed even when there is no media type on the
	//       stream. If there is no media type, we only need to fill in the dwFlags
	//       member of MFT_OUTPUT_STREAM_INFO. The other members depend on having a
	//       a valid media type.

	pStreamInfo->dwFlags =
		MFT_OUTPUT_STREAM_WHOLE_SAMPLES |
		MFT_OUTPUT_STREAM_SINGLE_SAMPLE_PER_BUFFER |
		MFT_OUTPUT_STREAM_FIXED_SAMPLE_SIZE;

	if (m_spOutputType == nullptr)
	{
		pStreamInfo->cbSize = 0;
	}
	else
	{
		pStreamInfo->cbSize = m_cbImageSize;
	}

	pStreamInfo->cbAlignment = 0;

	return S_OK;
}


//-------------------------------------------------------------------
// GetAttributes
// Returns the attributes for the MFT.
//-------------------------------------------------------------------

HRESULT CImagingEffect::GetAttributes(IMFAttributes **ppAttributes)
{
	if (ppAttributes == nullptr)
	{
		return E_POINTER;
	}

	AutoLock lock(m_critSec);

	*ppAttributes = m_spAttributes.Get();
	(*ppAttributes)->AddRef();

	return S_OK;
}


//-------------------------------------------------------------------
// GetInputStreamAttributes
// Returns stream-level attributes for an input stream.
//-------------------------------------------------------------------

HRESULT CImagingEffect::GetInputStreamAttributes(
	DWORD           dwInputStreamID,
	IMFAttributes   **ppAttributes
	)
{
	// This MFT does not support any stream-level attributes, so the method is not implemented.
	return E_NOTIMPL;
}


//-------------------------------------------------------------------
// GetOutputStreamAttributes
// Returns stream-level attributes for an output stream.
//-------------------------------------------------------------------

HRESULT CImagingEffect::GetOutputStreamAttributes(
	DWORD           dwOutputStreamID,
	IMFAttributes   **ppAttributes
	)
{
	// This MFT does not support any stream-level attributes, so the method is not implemented.
	return E_NOTIMPL;
}


//-------------------------------------------------------------------
// DeleteInputStream
//-------------------------------------------------------------------

HRESULT CImagingEffect::DeleteInputStream(DWORD dwStreamID)
{
	// This MFT has a fixed number of input streams, so the method is not supported.
	return E_NOTIMPL;
}


//-------------------------------------------------------------------
// AddInputStreams
//-------------------------------------------------------------------

HRESULT CImagingEffect::AddInputStreams(
	DWORD   cStreams,
	DWORD   *adwStreamIDs
	)
{
	// This MFT has a fixed number of output streams, so the method is not supported.
	return E_NOTIMPL;
}


//-------------------------------------------------------------------
// GetInputAvailableType
// Returns a preferred input type.
//-------------------------------------------------------------------

HRESULT CImagingEffect::GetInputAvailableType(
	DWORD           dwInputStreamID,
	DWORD           dwTypeIndex, // 0-based
	IMFMediaType    **ppType
	)
{
	HRESULT hr = S_OK;
	try
	{
		if (ppType == nullptr)
		{
			throw ref new InvalidArgumentException();
		}

		AutoLock lock(m_critSec);

		if (!IsValidInputStream(dwInputStreamID))
		{
			ThrowException(MF_E_INVALIDSTREAMNUMBER);
		}

		// If the output type is set, return that type as our preferred input type.
		if (m_spOutputType == nullptr)
		{
			// The output type is not set. Create a partial media type.
			*ppType = OnGetPartialType(dwTypeIndex).Detach();
		}
		else if (dwTypeIndex > 0)
		{
			return MF_E_NO_MORE_TYPES;
		}
		else
		{
			*ppType = m_spOutputType.Get();
			(*ppType)->AddRef();
		}
	}
	catch (Exception ^exc)
	{
		hr = exc->HResult;
	}

	return hr;
}



//-------------------------------------------------------------------
// GetOutputAvailableType
// Returns a preferred output type.
//-------------------------------------------------------------------

HRESULT CImagingEffect::GetOutputAvailableType(
	DWORD           dwOutputStreamID,
	DWORD           dwTypeIndex, // 0-based
	IMFMediaType    **ppType
	)
{
	HRESULT hr = S_OK;

	try
	{
		if (ppType == nullptr)
		{
			throw ref new InvalidArgumentException();
		}

		AutoLock lock(m_critSec);

		if (!IsValidOutputStream(dwOutputStreamID))
		{
			return MF_E_INVALIDSTREAMNUMBER;
		}

		if (m_spInputType == nullptr)
		{
			// The input type is not set. Create a partial media type.
			*ppType = OnGetPartialType(dwTypeIndex).Detach();
		}
		else if (dwTypeIndex > 0)
		{
			return MF_E_NO_MORE_TYPES;
		}
		else
		{
			*ppType = m_spInputType.Get();
			(*ppType)->AddRef();
		}
	}
	catch (Exception ^exc)
	{
		hr = exc->HResult;
	}

	return hr;
}


//-------------------------------------------------------------------
// SetInputType
//-------------------------------------------------------------------

HRESULT CImagingEffect::SetInputType(
	DWORD           dwInputStreamID,
	IMFMediaType    *pType, // Can be nullptr to clear the input type.
	DWORD           dwFlags
	)
{
	HRESULT hr = S_OK;

	try
	{
		// Validate flags.
		if (dwFlags & ~MFT_SET_TYPE_TEST_ONLY)
		{
			throw ref new InvalidArgumentException();
		}

		AutoLock lock(m_critSec);

		if (!IsValidInputStream(dwInputStreamID))
		{
			ThrowException(MF_E_INVALIDSTREAMNUMBER);
		}

		// Does the caller want us to set the type, or just test it?
		bool fReallySet = ((dwFlags & MFT_SET_TYPE_TEST_ONLY) == 0);

		// If we have an input sample, the client cannot change the type now.
		if (HasPendingOutput())
		{
			ThrowException(MF_E_TRANSFORM_CANNOT_CHANGE_MEDIATYPE_WHILE_PROCESSING);
		}

		// Validate the type, if non-nullptr.
		if (pType != nullptr)
		{
			OnCheckInputType(pType);
		}

		// The type is OK. Set the type, unless the caller was just testing.
		if (fReallySet)
		{
			OnSetInputType(pType);
			// When the type changes, end streaming.
			EndStreaming();
		}
	}
	catch (Exception ^exc)
	{
		hr = exc->HResult;
	}

	return hr;
}



//-------------------------------------------------------------------
// SetOutputType
//-------------------------------------------------------------------

HRESULT CImagingEffect::SetOutputType(
	DWORD           dwOutputStreamID,
	IMFMediaType    *pType, // Can be nullptr to clear the output type.
	DWORD           dwFlags
	)
{
	HRESULT hr = S_OK;

	try
	{
		if (!IsValidOutputStream(dwOutputStreamID))
		{
			return MF_E_INVALIDSTREAMNUMBER;
		}

		// Validate flags.
		if (dwFlags & ~MFT_SET_TYPE_TEST_ONLY)
		{
			return E_INVALIDARG;
		}

		AutoLock lock(m_critSec);

		// Does the caller want us to set the type, or just test it?
		bool fReallySet = ((dwFlags & MFT_SET_TYPE_TEST_ONLY) == 0);

		// If we have an input sample, the client cannot change the type now.
		if (HasPendingOutput())
		{
			ThrowException(MF_E_TRANSFORM_CANNOT_CHANGE_MEDIATYPE_WHILE_PROCESSING);
		}

		// Validate the type, if non-nullptr.
		if (pType != nullptr)
		{
			OnCheckOutputType(pType);
		}

		if (fReallySet)
		{
			// The type is OK.
			// Set the type, unless the caller was just testing.
			OnSetOutputType(pType);

			EndStreaming();
		}
	}
	catch (Exception ^exc)
	{
		hr = exc->HResult;
	}

	return hr;
}


//-------------------------------------------------------------------
// GetInputCurrentType
// Returns the current input type.
//-------------------------------------------------------------------

HRESULT CImagingEffect::GetInputCurrentType(
	DWORD           dwInputStreamID,
	IMFMediaType    **ppType
	)
{
	if (ppType == nullptr)
	{
		return E_POINTER;
	}

	HRESULT hr = S_OK;

	AutoLock lock(m_critSec);

	if (!IsValidInputStream(dwInputStreamID))
	{
		hr = MF_E_INVALIDSTREAMNUMBER;
	}
	else if (!m_spInputType)
	{
		hr = MF_E_TRANSFORM_TYPE_NOT_SET;
	}
	else
	{
		*ppType = m_spInputType.Get();
		(*ppType)->AddRef();
	}

	return hr;
}


//-------------------------------------------------------------------
// GetOutputCurrentType
// Returns the current output type.
//-------------------------------------------------------------------

HRESULT CImagingEffect::GetOutputCurrentType(
	DWORD           dwOutputStreamID,
	IMFMediaType    **ppType
	)
{
	if (ppType == nullptr)
	{
		return E_POINTER;
	}

	HRESULT hr = S_OK;

	AutoLock lock(m_critSec);

	if (!IsValidOutputStream(dwOutputStreamID))
	{
		hr = MF_E_INVALIDSTREAMNUMBER;
	}
	else if (!m_spOutputType)
	{
		hr = MF_E_TRANSFORM_TYPE_NOT_SET;
	}
	else
	{
		*ppType = m_spOutputType.Get();
		(*ppType)->AddRef();
	}

	return hr;
}


//-------------------------------------------------------------------
// GetInputStatus
// Query if the MFT is accepting more input.
//-------------------------------------------------------------------

HRESULT CImagingEffect::GetInputStatus(
	DWORD           dwInputStreamID,
	DWORD           *pdwFlags
	)
{
	if (pdwFlags == nullptr)
	{
		return E_POINTER;
	}

	AutoLock lock(m_critSec);

	if (!IsValidInputStream(dwInputStreamID))
	{
		return MF_E_INVALIDSTREAMNUMBER;
	}

	// If an input sample is already queued, do not accept another sample until the 
	// client calls ProcessOutput or Flush.

	// NOTE: It is possible for an MFT to accept more than one input sample. For 
	// example, this might be required in a video decoder if the frames do not 
	// arrive in temporal order. In the case, the decoder must hold a queue of 
	// samples. For the video effect, each sample is transformed independently, so
	// there is no reason to queue multiple input samples.

	if (m_spSample == nullptr)
	{
		*pdwFlags = MFT_INPUT_STATUS_ACCEPT_DATA;
	}
	else
	{
		*pdwFlags = 0;
	}

	return S_OK;
}



//-------------------------------------------------------------------
// GetOutputStatus
// Query if the MFT can produce output.
//-------------------------------------------------------------------

HRESULT CImagingEffect::GetOutputStatus(DWORD *pdwFlags)
{
	if (pdwFlags == nullptr)
	{
		return E_POINTER;
	}

	AutoLock lock(m_critSec);

	// The MFT can produce an output sample if (and only if) there an input sample.
	if (m_spSample != nullptr)
	{
		*pdwFlags = MFT_OUTPUT_STATUS_SAMPLE_READY;
	}
	else
	{
		*pdwFlags = 0;
	}

	return S_OK;
}


//-------------------------------------------------------------------
// SetOutputBounds
// Sets the range of time stamps that the MFT will output.
//-------------------------------------------------------------------

HRESULT CImagingEffect::SetOutputBounds(
	LONGLONG        hnsLowerBound,
	LONGLONG        hnsUpperBound
	)
{
	// Implementation of this method is optional.
	return E_NOTIMPL;
}


//-------------------------------------------------------------------
// ProcessEvent
// Sends an event to an input stream.
//-------------------------------------------------------------------

HRESULT CImagingEffect::ProcessEvent(
	DWORD              dwInputStreamID,
	IMFMediaEvent      *pEvent
	)
{
	// This MFT does not handle any stream events, so the method can
	// return E_NOTIMPL. This tells the pipeline that it can stop
	// sending any more events to this MFT.
	return E_NOTIMPL;
}


//-------------------------------------------------------------------
// ProcessMessage
//-------------------------------------------------------------------

HRESULT CImagingEffect::ProcessMessage(
	MFT_MESSAGE_TYPE    eMessage,
	ULONG_PTR           ulParam
	)
{
	AutoLock lock(m_critSec);

	HRESULT hr = S_OK;

	try
	{
		switch (eMessage)
		{
		case MFT_MESSAGE_COMMAND_FLUSH:
			// Flush the MFT.
			OnFlush();
			break;

		case MFT_MESSAGE_COMMAND_DRAIN:
			// Drain: Tells the MFT to reject further input until all pending samples are
			// processed. That is our default behavior already, so there is nothing to do.
			//
			// For a decoder that accepts a queue of samples, the MFT might need to drain
			// the queue in response to this command.
			break;

		case MFT_MESSAGE_SET_D3D_MANAGER:
			// Sets a pointer to the IDirect3DDeviceManager9 interface.

			// The pipeline should never send this message unless the MFT sets the MF_SA_D3D_AWARE 
			// attribute set to TRUE. Because this MFT does not set MF_SA_D3D_AWARE, it is an error
			// to send the MFT_MESSAGE_SET_D3D_MANAGER message to the MFT. Return an error code in
			// this case.

			// NOTE: If this MFT were D3D-enabled, it would cache the IMFDXGIDeviceManager
			// pointer for use during streaming.

			ThrowException(E_NOTIMPL);
			break;

		case MFT_MESSAGE_NOTIFY_BEGIN_STREAMING:
			BeginStreaming();
			break;

		case MFT_MESSAGE_NOTIFY_END_STREAMING:
			EndStreaming();
			break;

			// The next two messages do not require any action from this MFT.

		case MFT_MESSAGE_NOTIFY_END_OF_STREAM:
			break;

		case MFT_MESSAGE_NOTIFY_START_OF_STREAM:
			break;
		}
	}
	catch (Exception ^exc)
	{
		hr = exc->HResult;
	}

	return hr;
}


//-------------------------------------------------------------------
// ProcessInput
// Process an input sample.
//-------------------------------------------------------------------

HRESULT CImagingEffect::ProcessInput(
	DWORD               dwInputStreamID,
	IMFSample           *pSample,
	DWORD               dwFlags
	)
{
	HRESULT hr = S_OK;

	try
	{
		// Check input parameters.
		if (pSample == nullptr)
		{
			throw ref new InvalidArgumentException();
		}

		if (dwFlags != 0)
		{
			throw ref new InvalidArgumentException(); // dwFlags is reserved and must be zero.
		}

		AutoLock lock(m_critSec);

		// Validate the input stream number.
		if (!IsValidInputStream(dwInputStreamID))
		{
			ThrowException(MF_E_INVALIDSTREAMNUMBER);
		}

		// Check for valid media types.
		// The client must set input and output types before calling ProcessInput.
		if (m_spInputType == nullptr || m_spOutputType == nullptr)
		{
			ThrowException(MF_E_NOTACCEPTING);
		}

		// Check if an input sample is already queued.
		if (m_spSample != nullptr)
		{
			ThrowException(MF_E_NOTACCEPTING);   // We already have an input sample.
		}

		// Initialize streaming.
		BeginStreaming();

		// Cache the sample. We do the actual work in ProcessOutput.
		m_spSample = pSample;
	}
	catch (Exception ^exc)
	{
		hr = exc->HResult;
	}

	return hr;
}


//-------------------------------------------------------------------
// ProcessOutput
// Process an output sample.
//-------------------------------------------------------------------

HRESULT CImagingEffect::ProcessOutput(
	DWORD                   dwFlags,
	DWORD                   cOutputBufferCount,
	MFT_OUTPUT_DATA_BUFFER  *pOutputSamples, // one per stream
	DWORD                   *pdwStatus
	)
{
	HRESULT hr = S_OK;
	AutoLock lock(m_critSec);

	try
	{
		// Check input parameters...

		// This MFT does not accept any flags for the dwFlags parameter.

		// The only defined flag is MFT_PROCESS_OUTPUT_DISCARD_WHEN_NO_BUFFER. This flag 
		// applies only when the MFT marks an output stream as lazy or optional. But this
		// MFT has no lazy or optional streams, so the flag is not valid.

		if (dwFlags != 0)
		{
			throw ref new InvalidArgumentException();
		}

		if (pOutputSamples == nullptr || pdwStatus == nullptr)
		{
			throw ref new InvalidArgumentException();
		}

		// There must be exactly one output buffer.
		if (cOutputBufferCount != 1)
		{
			throw ref new InvalidArgumentException();
		}

		// It must contain a sample.
		if (pOutputSamples[0].pSample == nullptr)
		{
			throw ref new InvalidArgumentException();
		}

		ComPtr<IMFMediaBuffer> spInput;
		ComPtr<IMFMediaBuffer> spOutput;

		// There must be an input sample available for processing.
		if (m_spSample == nullptr)
		{
			return MF_E_TRANSFORM_NEED_MORE_INPUT;
		}

		// Initialize streaming.
		BeginStreaming();

		// Get the input buffer.
		ThrowIfError(m_spSample->ConvertToContiguousBuffer(&spInput));

		// Get the output buffer.
		ThrowIfError(pOutputSamples[0].pSample->ConvertToContiguousBuffer(&spOutput));

		OnProcessOutput(spInput.Get(), spOutput.Get());

		// Set status flags.
		pOutputSamples[0].dwStatus = 0;
		*pdwStatus = 0;


		// Copy the duration and time stamp from the input sample, if present.

		LONGLONG hnsDuration = 0;
		LONGLONG hnsTime = 0;

		if (SUCCEEDED(m_spSample->GetSampleDuration(&hnsDuration)))
		{
			ThrowIfError(pOutputSamples[0].pSample->SetSampleDuration(hnsDuration));
		}

		if (SUCCEEDED(m_spSample->GetSampleTime(&hnsTime)))
		{
			ThrowIfError(pOutputSamples[0].pSample->SetSampleTime(hnsTime));
		}
	}
	catch (Exception ^exc)
	{
		hr = exc->HResult;
	}

	m_spSample.Reset(); // Release our input sample.

	return hr;
}

// PRIVATE METHODS

// All methods that follow are private to this MFT and are not part of the IMFTransform interface.

// Create a partial media type from our list.
//
// dwTypeIndex: Index into the list of peferred media types.
// ppmt:        Receives a pointer to the media type.

ComPtr<IMFMediaType> CImagingEffect::OnGetPartialType(DWORD dwTypeIndex)
{
	if (dwTypeIndex >= ARRAYSIZE(g_MediaSubtypes))
	{
		ThrowException(MF_E_NO_MORE_TYPES);
	}

	ComPtr<IMFMediaType> spMT;

	ThrowIfError(MFCreateMediaType(&spMT));

	ThrowIfError(spMT->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video));

	ThrowIfError(spMT->SetGUID(MF_MT_SUBTYPE, g_MediaSubtypes[dwTypeIndex]));

	return spMT;
}


// Validate an input media type.

void CImagingEffect::OnCheckInputType(IMFMediaType *pmt)
{
	assert(pmt != nullptr);

	// If the output type is set, see if they match.
	if (m_spOutputType != nullptr)
	{
		DWORD flags = 0;
		// IsEqual can return S_FALSE. Treat this as failure.
		if (pmt->IsEqual(m_spOutputType.Get(), &flags) != S_OK)
		{
			ThrowException(MF_E_INVALIDMEDIATYPE);
		}
	}
	else
	{
		// Output type is not set. Just check this type.
		OnCheckMediaType(pmt);
	}
}


// Validate an output media type.

void CImagingEffect::OnCheckOutputType(IMFMediaType *pmt)
{
	assert(pmt != nullptr);

	// If the input type is set, see if they match.
	if (m_spInputType != nullptr)
	{
		DWORD flags = 0;
		// IsEqual can return S_FALSE. Treat this as failure.
		if (pmt->IsEqual(m_spInputType.Get(), &flags) != S_OK)
		{
			ThrowException(MF_E_INVALIDMEDIATYPE);
		}
	}
	else
	{
		// Input type is not set. Just check this type.
		OnCheckMediaType(pmt);
	}
}


// Validate a media type (input or output)

void CImagingEffect::OnCheckMediaType(IMFMediaType *pmt)
{
	bool fFoundMatchingSubtype = false;

	// Major type must be video.
	GUID major_type;
	ThrowIfError(pmt->GetGUID(MF_MT_MAJOR_TYPE, &major_type));

	if (major_type != MFMediaType_Video)
	{
		ThrowException(MF_E_INVALIDMEDIATYPE);
	}

	// Subtype must be one of the subtypes in our global list.

	// Get the subtype GUID.
	GUID subtype;
	ThrowIfError(pmt->GetGUID(MF_MT_SUBTYPE, &subtype));

	// Look for the subtype in our list of accepted types.
	for (DWORD i = 0; i < ARRAYSIZE(g_MediaSubtypes); i++)
	{
		if (subtype == g_MediaSubtypes[i])
		{
			fFoundMatchingSubtype = true;
			break;
		}
	}

	if (!fFoundMatchingSubtype)
	{
		ThrowException(MF_E_INVALIDMEDIATYPE); // The MFT does not support this subtype.
	}

	// Reject single-field media types. 
	UINT32 interlace = MFGetAttributeUINT32(pmt, MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
	if (interlace == MFVideoInterlace_FieldSingleUpper || interlace == MFVideoInterlace_FieldSingleLower)
	{
		ThrowException(MF_E_INVALIDMEDIATYPE);
	}
}


// Set or clear the input media type.
//
// Prerequisite: The input type was already validated.

void CImagingEffect::OnSetInputType(IMFMediaType *pmt)
{
	// if pmt is nullptr, clear the type.
	// if pmt is non-nullptr, set the type.
	m_spInputType = pmt;

	// Update the format information.
	UpdateFormatInfo();
}


// Set or clears the output media type.
//
// Prerequisite: The output type was already validated.

void CImagingEffect::OnSetOutputType(IMFMediaType *pmt)
{
	// If pmt is nullptr, clear the type. Otherwise, set the type.
	m_spOutputType = pmt;
}


// Initialize streaming parameters.
//
// This method is called if the client sends the MFT_MESSAGE_NOTIFY_BEGIN_STREAMING
// message, or when the client processes a sample, whichever happens first.

void CImagingEffect::BeginStreaming()
{
	if (!m_fStreamingInitialized)
	{
		m_rcDest = D2D1::RectU(0, 0, m_imageWidthInPixels, m_imageHeightInPixels);
		m_fStreamingInitialized = true;
	}
}


// End streaming. 

// This method is called if the client sends an MFT_MESSAGE_NOTIFY_END_STREAMING
// message, or when the media type changes. In general, it should be called whenever
// the streaming parameters need to be reset.

void CImagingEffect::EndStreaming()
{
	m_fStreamingInitialized = false;
}



// Generate output data.

void CImagingEffect::OnProcessOutput(IMFMediaBuffer *pIn, IMFMediaBuffer *pOut)
{
	// Stride if the buffer does not support IMF2DBuffer
	const LONG lDefaultStride = GetDefaultStride(m_spInputType.Get());

	// Helper objects to lock the buffers.
	VideoBufferLock inputLock(pIn, MF2DBuffer_LockFlags_Read, m_imageHeightInPixels, lDefaultStride);
	VideoBufferLock outputLock(pOut, MF2DBuffer_LockFlags_Write, m_imageHeightInPixels, lDefaultStride);

	// Invoke the image transform function.
	assert(m_pTransformFn != nullptr);
	if (m_pTransformFn)
	{
		(*m_pTransformFn)(m_rcDest, outputLock.GetTopRow(), outputLock.GetStride(), inputLock.GetTopRow(), inputLock.GetStride(), m_imageWidthInPixels, m_imageHeightInPixels, m_imageProviders);
	}
	else
	{
		ThrowException(E_UNEXPECTED);
	}

	// Set the data size on the output buffer.
	ThrowIfError(pOut->SetCurrentLength(m_cbImageSize));

}


// Flush the MFT.

void CImagingEffect::OnFlush()
{
	// For this MFT, flushing just means releasing the input sample.
	m_spSample.Reset();
}


// Update the format information. This method is called whenever the
// input type is set.

void CImagingEffect::UpdateFormatInfo()
{
	GUID subtype = GUID_NULL;

	m_imageWidthInPixels = 0;
	m_imageHeightInPixels = 0;
	m_cbImageSize = 0;

	m_pTransformFn = nullptr;

	if (m_spInputType != nullptr)
	{
		ThrowIfError(m_spInputType->GetGUID(MF_MT_SUBTYPE, &subtype));
		if (subtype == MFVideoFormat_YUY2)
		{
			m_pTransformFn = TransformImage_YUY2;
		}
		/*else if (subtype == MFVideoFormat_UYVY)
		{
		m_pTransformFn = TransformImage_UYVY;
		}*/
		else if (subtype == MFVideoFormat_NV12)
		{
			m_pTransformFn = TransformImage_NV12;
		}
		else
		{
			ThrowException(E_UNEXPECTED);
		}

		ThrowIfError(MFGetAttributeSize(m_spInputType.Get(), MF_MT_FRAME_SIZE, &m_imageWidthInPixels, &m_imageHeightInPixels));

		// Calculate the image size (not including padding)
		m_cbImageSize = GetImageSize(subtype.Data1, m_imageWidthInPixels, m_imageHeightInPixels);
	}
}


// Calculate the size of the buffer needed to store the image.

// fcc: The FOURCC code of the video format.

DWORD GetImageSize(DWORD fcc, UINT32 width, UINT32 height)
{
	switch (fcc)
	{
	case FOURCC_YUY2:
	case FOURCC_UYVY:
		// check overflow
		if ((width > MAXDWORD / 2) || (width * 2 > MAXDWORD / height))
		{
			throw ref new InvalidArgumentException();
		}
		else
		{
			// 16 bpp
			return width * height * 2;
		}

	case FOURCC_NV12:
		// check overflow
		if ((height / 2 > MAXDWORD - height) || ((height + height / 2) > MAXDWORD / width))
		{
			throw ref new InvalidArgumentException();
		}
		else
		{
			// 12 bpp
			return width * (height + (height / 2));
		}

	default:
		// Unsupported type.
		ThrowException(MF_E_INVALIDTYPE);
	}

	return 0;
}

// Get the default stride for a video format. 
LONG GetDefaultStride(IMFMediaType *pType)
{
	LONG lStride = 0;

	// Try to get the default stride from the media type.
	if (FAILED(pType->GetUINT32(MF_MT_DEFAULT_STRIDE, (UINT32*)&lStride)))
	{
		// Attribute not set. Try to calculate the default stride.
		GUID subtype = GUID_NULL;

		UINT32 width = 0;
		UINT32 height = 0;

		// Get the subtype and the image size.
		ThrowIfError(pType->GetGUID(MF_MT_SUBTYPE, &subtype));
		ThrowIfError(MFGetAttributeSize(pType, MF_MT_FRAME_SIZE, &width, &height));
		if (subtype == MFVideoFormat_NV12)
		{
			lStride = width;
		}
		else if (subtype == MFVideoFormat_YUY2 || subtype == MFVideoFormat_UYVY)
		{
			lStride = ((width * 2) + 3) & ~3;
		}
		else
		{
			throw ref new InvalidArgumentException();
		}

		// Set the attribute for later reference.
		(void)pType->SetUINT32(MF_MT_DEFAULT_STRIDE, UINT32(lStride));
	}

	return lStride;
}
