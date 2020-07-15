//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md 
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "RenderSurfaceControl.h"
#include "Core\Rtt_Assert.h"
#include "WinString.h"
#include <exception>
// STEVE CHANGE TODO
#include <GL\glew.h>
#include <GL\wglew.h>
#include <GL\gl.h>
#include <GL\glu.h>


#ifdef free
#undef free
#endif

#include <vulkan\vulkan.h>
#include "CoronaLog.h"
#include "Renderer/Rtt_VulkanState.h"
#include <algorithm>
#include <utility>
#include <vector>
// /STEVE CHANGE

namespace Interop { namespace UI {

#pragma region Constructors/Destructors
RenderSurfaceControl::RenderSurfaceControl(HWND windowHandle)
:	Control(windowHandle),
	fReceivedMessageEventHandler(this, &RenderSurfaceControl::OnReceivedMessage),
	fRenderFrameEventHandlerPointer(nullptr),
	fMainDeviceContextHandle(nullptr),
	fPaintDeviceContextHandle(nullptr),
	fRenderingContextHandle(nullptr),
// STEVE CHANGE
	fVulkanState(nullptr)
// /STEVE CHANGE
{
	// Add event handlers.
	GetReceivedMessageEventHandlers().Add(&fReceivedMessageEventHandler);

	// Create an OpenGL context and bind it to the given control.
	CreateContext();
}

RenderSurfaceControl::~RenderSurfaceControl()
{
	// Remove event handlers.
	GetReceivedMessageEventHandlers().Remove(&fReceivedMessageEventHandler);

	// Destroy the OpenGL context.
	DestroyContext();
}

#pragma endregion


#pragma region Public Methods
bool RenderSurfaceControl::CanRender() const
{
	return (fRenderingContextHandle != nullptr);
}

RenderSurfaceControl::Version RenderSurfaceControl::GetRendererVersion() const
{
	return fRendererVersion;
}

void RenderSurfaceControl::SetRenderFrameHandler(RenderSurfaceControl::RenderFrameEvent::Handler *handlerPointer)
{
	fRenderFrameEventHandlerPointer = handlerPointer;
}

void RenderSurfaceControl::SelectRenderingContext()
{
	// Attempt to select this surface's rendering context.
	BOOL wasSelected = FALSE;
	if (fRenderingContextHandle)
	{
// STEVE CHANGE TODO
		// Favor the Win32 BeginPaint() function's device context over our main device context, if available.
		if (fPaintDeviceContextHandle)
		{
			wasSelected = ::wglMakeCurrent(fPaintDeviceContextHandle, fRenderingContextHandle);
		}
		else if (fMainDeviceContextHandle)
		{
			wasSelected = ::wglMakeCurrent(fMainDeviceContextHandle, fRenderingContextHandle);
		}
// /STEVE CHANGE
		// Log an error if we've failed to select a rendering context.
		// Note: This can happen while the control is being destroyed.
		if (!wasSelected)
		{
			LPWSTR utf16Buffer;
			auto errorCode = ::GetLastError();
			::FormatMessageW(
					FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
					nullptr, errorCode,
					MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
					(LPWSTR)&utf16Buffer, 0, nullptr);
			if (utf16Buffer && utf16Buffer[0])
			{
				WinString stringConverter;
				stringConverter.SetUTF16(utf16Buffer);
				Rtt_LogException(
						"Failed to select OpenGL rendering context. Reason:\r\n  %s\r\n", stringConverter.GetUTF8());
			}
			else
			{
				Rtt_LogException("Failed to select OpenGL rendering context.\r\n");
			}
			::LocalFree(utf16Buffer);
		}
	}

	// If we've failed, then select a null context so that the caller won't clobber another rendering context by mistake.
	if (!wasSelected)
	{
		// STEVE CHANGE TODO
		::wglMakeCurrent(nullptr, nullptr);
		// /STEVE CHANGE
	}
}

void RenderSurfaceControl::SwapBuffers()
{
	if (fPaintDeviceContextHandle)
	{
		::SwapBuffers(fPaintDeviceContextHandle);
	}
	else if (fMainDeviceContextHandle)
	{
		::SwapBuffers(fMainDeviceContextHandle);
	}
}

void RenderSurfaceControl::RequestRender()
{
	auto windowHandle = GetWindowHandle();
	if (windowHandle)
	{
		::InvalidateRect(windowHandle, nullptr, FALSE);
	}
}

#pragma endregion


#pragma region Protected Methods
void RenderSurfaceControl::OnRaisedDestroyingEvent()
{
	// Destroy the rendering context before the control/window gets destroyed.
	DestroyContext();

	// Let the base class perform its final tasks.
	Control::OnRaisedDestroyingEvent();
}

#pragma endregion

#pragma region Private Methods
// STEVE CHANGE
const char *
StringIdentity( const char * str )
{
	return str;
}

template<typename I, typename F> bool
FindString( I & i1, I & i2, const char * str, F && getString )
{
	return std::find_if( i1, i2, [str, getString](const I::value_type & other) { return strcmp( str, getString( other ) ) == 0; } ) != i2;
}

static void
CollectExtensions(std::vector<const char *> & extensions, std::vector<const char *> & optional, const std::vector<VkExtensionProperties> & extensionProps)
{
	auto optionalEnd = std::remove_if(optional.begin(), optional.end(), [&extensions](const char * name)
	{
		return FindString(extensions.begin(), extensions.end(), name, StringIdentity);
	});
		
	for (auto & props : extensionProps)
	{
		const char * name = props.extensionName;

		if (FindString(optional.begin(), optionalEnd, props.extensionName, StringIdentity))
		{
			extensions.push_back(props.extensionName);
		}
	}
}

static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData
) {
    CoronaLog("validation layer: %s", pCallbackData->pMessage);

/*
	VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT: Some event has happened that is unrelated to the specification or performance
	VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT: Something has happened that violates the specification or indicates a possible mistake
	VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT: Potential non-optimal use of Vulkan
*/

    return VK_FALSE;
}

static VkApplicationInfo
AppInfo()
{
	VkApplicationInfo appInfo = {};

	appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	appInfo.pApplicationName = "Solar App"; // TODO?
	appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
	appInfo.pEngineName = "Solar2D";
	appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
	appInfo.apiVersion = VK_API_VERSION_1_0;

	return appInfo;
}

#ifndef NDEBUG
static std::pair< VkInstance, VkDebugUtilsMessengerEXT >
#else
static VkInstance
#endif
MakeInstance( VkApplicationInfo * appInfo, const VkAllocationCallbacks * allocator )
{
	VkInstanceCreateInfo createInfo = {};

	createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	createInfo.pApplicationInfo = appInfo;

	std::vector< const char * > extensions = { VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME }, optional;
	unsigned int extensionCount = 0U;

	vkEnumerateInstanceExtensionProperties(NULL, &extensionCount, NULL);

	std::vector<VkExtensionProperties> extensionProps(extensionCount);

	vkEnumerateInstanceExtensionProperties(NULL, &extensionCount, extensionProps.data());

	VkInstance instance = VK_NULL_HANDLE;

#ifndef NDEBUG
	extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif

	CollectExtensions(extensions, optional, extensionProps);

	createInfo.enabledExtensionCount = extensions.size();
	createInfo.ppEnabledExtensionNames = extensions.data();

	bool ok = true;

#ifndef NDEBUG
    uint32_t layerCount;

    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
	
	const std::vector<const char*> validationLayers = { "VK_LAYER_KHRONOS_validation" };
    std::vector<VkLayerProperties> availableLayers(layerCount);

    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

	for (const char * layerName : validationLayers)
	{
		if (!FindString(availableLayers.begin(), availableLayers.end(), layerName, [](const VkLayerProperties & props)
		{
			return props.layerName;
		}))
		{
			CoronaLog( "Unable to find layer %s", layerName );

			ok = false;

			break;
		}
	}

	createInfo.enabledLayerCount = validationLayers.size();
	createInfo.ppEnabledLayerNames = validationLayers.data();

	VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo = {};

	debugCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
	debugCreateInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
	debugCreateInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
	debugCreateInfo.pfnUserCallback = DebugCallback;

	createInfo.pNext = (VkDebugUtilsMessengerCreateInfoEXT *) &debugCreateInfo;
#endif

	if (ok && vkCreateInstance(&createInfo, allocator, &instance) != VK_SUCCESS)
	{
		CoronaLog( "Failed to create instance!\n" );

		ok = false;
	}

#ifndef NDEBUG
	VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;

	if (ok)
	{
		auto func = (PFN_vkCreateDebugUtilsMessengerEXT) vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");

		if (!func || VK_SUCCESS == func(instance, &debugCreateInfo, allocator, &messenger) != VK_SUCCESS)
		{
			CoronaLog( "Failed to create debug messenger!\n" );

			vkDestroyInstance(instance, allocator);

			instance = VK_NULL_HANDLE;
		}
	}

	return std::make_pair( instance, messenger );
#else
	return instance;
#endif
}

static VkSurfaceKHR
MakeSurface( VkInstance instance, HWND handle, const VkAllocationCallbacks * allocator )
{
	VkWin32SurfaceCreateInfoKHR createSurfaceInfo = {};

	createSurfaceInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
	createSurfaceInfo.hwnd = handle;
	createSurfaceInfo.hinstance = GetModuleHandle(nullptr);

	VkSurfaceKHR surface;

	if (vkCreateWin32SurfaceKHR(instance, &createSurfaceInfo, allocator, &surface) != VK_SUCCESS)
	{
		CoronaLog("Failed to create window surface!");

		surface = VK_NULL_HANDLE;
	}

	return surface;
}

struct Queues {
	Queues()
:	fGraphicsQueue( ~0U ),
	fPresentQueue( ~0U )
	{
	}

	uint32_t fGraphicsQueue;
	uint32_t fPresentQueue;

	bool isComplete() const { return fGraphicsQueue != ~0U && fPresentQueue != ~0U; }

	std::vector<uint32_t> GetFamilies() const
	{
		std::vector<uint32_t> families;

		if (isComplete())
		{
			families.push_back(fGraphicsQueue);

			if (fGraphicsQueue != fPresentQueue)
			{
				families.push_back(fPresentQueue);
			}
		}

		return families;
	}
};

static bool
IsSuitableDevice( VkPhysicalDevice device, VkSurfaceKHR surface, Queues & queues )
{
	uint32_t queueFamilyCount = 0U;

	vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

	std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);

	vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

	for (uint32_t i = 0; i < queueFamilyCount; ++i)
	{
		if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
		{
			queues.fGraphicsQueue = i;
		}

		VkBool32 supported = VK_FALSE;

		vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &supported);

		if (supported)
		{
			queues.fPresentQueue = i;
		}
	}

	if (!queues.isComplete())
	{
		return false;
	}

	uint32_t extensionCount = 0U;

    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

    std::vector<VkExtensionProperties> availableExtensions(extensionCount);

    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

	std::vector< const char * > extensions, required = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

	CollectExtensions(extensions, required, availableExtensions);

	if (extensions.size() != required.size())
	{
		return false;
	}

	uint32_t formatCount = 0U, presentModeCount = 0U;

	vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr);
	vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, nullptr);

	return formatCount != 0U && presentModeCount != 0U;
}

static std::pair< VkPhysicalDevice, Queues >
ChoosePhysicalDevice( VkInstance instance, VkSurfaceKHR surface )
{
	uint32_t deviceCount = 0U;

	vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);

	if (0U == deviceCount)
	{
		CoronaLog("Failed to find GPUs with Vulkan support!");

		return std::make_pair(VkPhysicalDevice(VK_NULL_HANDLE), Queues());
	}

	std::vector<VkPhysicalDevice> devices(deviceCount);

	vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());
	
	VkPhysicalDevice bestDevice = VK_NULL_HANDLE;
	uint32_t bestScore = 0U;
	Queues bestQueues;

	for (const VkPhysicalDevice & device : devices)
	{
		Queues queues;

		if (!IsSuitableDevice(device, surface, queues))
		{
			continue;
		}

		VkPhysicalDeviceProperties deviceProperties;
		VkPhysicalDeviceFeatures deviceFeatures;

	    vkGetPhysicalDeviceProperties(device, &deviceProperties);
		vkGetPhysicalDeviceFeatures(device, &deviceFeatures);
		
		uint32_t score = 0U;

		// Discrete GPUs have a significant performance advantage
		if (VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU == deviceProperties.deviceType)
		{
			score += 1000U;
		}

		// Maximum possible size of textures affects graphics quality
		score += deviceProperties.limits.maxImageDimension2D;

		// other ideas: ETC2 etc. texture compression

		if (score > bestScore)
		{
			bestDevice = device;
			bestScore = score;
			bestQueues = queues;
		}
	}

	return std::make_pair(bestDevice, bestQueues);
}

static VkDevice
MakeLogicalDevice( VkPhysicalDevice physicalDevice, const std::vector<uint32_t> & families, const VkAllocationCallbacks * allocator )
{
	VkDeviceCreateInfo createDeviceInfo = {};

	createDeviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;

	std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;

	for (uint32_t index : families)
	{
		VkDeviceQueueCreateInfo queueCreateInfo = {};

		queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		queueCreateInfo.queueFamilyIndex = index;
		queueCreateInfo.queueCount = 1;

		queueCreateInfos.push_back(queueCreateInfo);
	}

	createDeviceInfo.pQueueCreateInfos = queueCreateInfos.data();
	createDeviceInfo.queueCreateInfoCount = queueCreateInfos.size();

	VkPhysicalDeviceFeatures deviceFeatures = {};

	createDeviceInfo.pEnabledFeatures = &deviceFeatures;

	VkDevice device;

	if (vkCreateDevice(physicalDevice, &createDeviceInfo, allocator, &device) != VK_SUCCESS)
	{
		CoronaLog("Failed to create logical device!");

		return VK_NULL_HANDLE;
	}

	return device;
}

bool RenderSurfaceControl::CreateVulkanState()
{
	Rtt::VulkanState * state = Rtt_NEW( NULL, Rtt::VulkanState );
	
	fVulkanState = state; // if we encounter an error we'll need to destroy this, so assign it early

	VkAllocationCallbacks * allocator = NULL; // TODO

	VkInstanceCreateInfo createInfo = {};
	VkApplicationInfo appInfo = AppInfo();
	auto instanceData = MakeInstance(&appInfo, allocator);

#ifndef NDEBUG
	state->SetDebugMessenger(instanceData.second);

	VkInstance instance = instanceData.first;
#else
	VkInstance instance = instanceData;
#endif

	if (instance != VK_NULL_HANDLE)
	{
		state->SetInstance(instance);

		VkSurfaceKHR surface = MakeSurface(instance, GetWindowHandle(), allocator);

		if (surface != VK_NULL_HANDLE)
		{
			state->SetSurface(surface);

			auto physicalDeviceData = ChoosePhysicalDevice(instance, surface);

			if (physicalDeviceData.first != VK_NULL_HANDLE)
			{
				state->SetPhysicalDevice(physicalDeviceData.first);

				VkDevice device = MakeLogicalDevice(physicalDeviceData.first, physicalDeviceData.second.GetFamilies(), allocator);

				if (device != VK_NULL_HANDLE)
				{
					VkQueue graphicsQueue, presentQueue;

					vkGetDeviceQueue(device, physicalDeviceData.second.fGraphicsQueue, 0, &graphicsQueue);
					vkGetDeviceQueue(device, physicalDeviceData.second.fPresentQueue, 0, &presentQueue);

					state->SetGraphicsQueue(graphicsQueue);
					state->SetPresentQueue(presentQueue);

					return true;
				}
			}
		}
	}

	return false;
}

// /STEVE CHANGE
void RenderSurfaceControl::CreateContext()
{
	// Fetch this control's window handle.
	auto windowHandle = GetWindowHandle();
	if (!windowHandle)
	{
		return;
	}

	// Destroy the last OpenGL context that was created.
	DestroyContext();
// STEVE CHANGE
	if (false) // wantVulkan
	{
		if (!CreateVulkanState())
		{
			// clean up
				// only preferred?
				// required?
		}
	}
// /STEVE CHANGE
	// Query the video hardware for multisampling result.
	auto multisampleTestResult = FetchMultisampleFormat();

	// Fetch the control's device context.
	fMainDeviceContextHandle = ::GetDC(windowHandle);
	if (!fMainDeviceContextHandle)
	{
		return;
	}

	// Select a good pixel format.
	PIXELFORMATDESCRIPTOR pixelFormatDescriptor {};
	pixelFormatDescriptor.nSize = sizeof(pixelFormatDescriptor);
	pixelFormatDescriptor.nVersion = 1;
	pixelFormatDescriptor.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
	pixelFormatDescriptor.iPixelType = PFD_TYPE_RGBA;
	pixelFormatDescriptor.cColorBits = 24;
	pixelFormatDescriptor.cDepthBits = 16;
	pixelFormatDescriptor.iLayerType = PFD_MAIN_PLANE;
	int pixelFormatIndex = ::ChoosePixelFormat(fMainDeviceContextHandle, &pixelFormatDescriptor);
	if (0 == pixelFormatIndex)
	{
		DestroyContext();
		return;
	}

	// Assign a pixel format to the device context.
	if (multisampleTestResult.IsSupported)
	{
		pixelFormatIndex = multisampleTestResult.PixelFormatIndex;
	}
	BOOL wasFormatSet = ::SetPixelFormat(fMainDeviceContextHandle, pixelFormatIndex, &pixelFormatDescriptor);
	if (!wasFormatSet)
	{
		DestroyContext();
		return;
	}

	// Create and enable the OpenGL rendering context.
// STEVE CHANGE TODO
	fRenderingContextHandle = ::wglCreateContext(fMainDeviceContextHandle);
// /STEVE CHANGE
	if (!fRenderingContextHandle)
	{
		LPWSTR utf16Buffer;
		auto errorCode = ::GetLastError();
		::FormatMessageW(
				FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
				nullptr, errorCode,
				MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
				(LPWSTR)&utf16Buffer, 0, nullptr);
		if (utf16Buffer && utf16Buffer[0])
		{
			WinString stringConverter;
			stringConverter.SetUTF16(utf16Buffer);
			Rtt_LogException("Failed to create OpenGL rendering context. Reason:\r\n  %s\r\n", stringConverter.GetUTF8());
		}
		else
		{
			Rtt_LogException("Failed to create OpenGL rendering context.\r\n");
		}
		::LocalFree(utf16Buffer);
	}
// STEVE CHANGE TODO
	// Select the newly created OpenGL context.
	::wglMakeCurrent(fMainDeviceContextHandle, fRenderingContextHandle);

	// Load OpenGL extensions.
	glewInit();

	// Fetch the OpenGL driver's version.
	const char* versionString = (const char*)glGetString(GL_VERSION);
// STEVE CHANGE
	fRendererVersion.SetString(versionString);
	fRendererVersion.SetMajorNumber(0);
	fRendererVersion.SetMinorNumber(0);
	if (versionString && (versionString[0] != '\0'))
	{
		try
		{
			int majorNumber = 0;
			int minorNumber = 0;
			sscanf_s(fRendererVersion.GetString(), "%d.%d", &majorNumber, &minorNumber);
			fRendererVersion.SetMajorNumber(majorNumber);
			fRendererVersion.SetMinorNumber(minorNumber);
		}
		catch (...) {}
	}
}

void RenderSurfaceControl::DestroyContext()
{
// STEVE CHANGE
	Rtt::VulkanState * state = static_cast< Rtt::VulkanState * >( fVulkanState );

	Rtt_DELETE( state );
// /STEVE CHANGE
	// Fetch this control's window handle.
	auto windowHandle = GetWindowHandle();
// STEVE CHANGE
	// Destroy the OpenGL context.
	::wglMakeCurrent(nullptr, nullptr);
	if (fRenderingContextHandle)
	{
		Rtt_ASSERT(!fVulkanState);

		::wglDeleteContext(fRenderingContextHandle);
		fRenderingContextHandle = nullptr;
	}

	fVulkanState = nullptr;
// /STEVE CHANGE
	if (fMainDeviceContextHandle)
	{
		if (windowHandle)
		{
			::ReleaseDC(windowHandle, fMainDeviceContextHandle);
		}
		fMainDeviceContextHandle = nullptr;
	}
	fPaintDeviceContextHandle = nullptr;
	fRendererVersion.SetString(nullptr);
	fRendererVersion.SetMajorNumber(0);
	fRendererVersion.SetMinorNumber(0);

	// Let the window re-paint itself now that we've detached from it.
	if (windowHandle)
	{
		::InvalidateRect(windowHandle, nullptr, FALSE);
	}
}

static HMODULE GetLibraryModuleHandle()
{
	HMODULE moduleHandle = nullptr;
	DWORD flags = GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT;
	::GetModuleHandleExW(flags, (LPCWSTR)GetLibraryModuleHandle, &moduleHandle);
	return moduleHandle;
}

RenderSurfaceControl::FetchMultisampleFormatResult RenderSurfaceControl::FetchMultisampleFormat()
{
	// Initialize a result value to "not supported".
	FetchMultisampleFormatResult result;
	result.IsSupported = false;

	// Fetch this control's window handle.
	auto windowHandle = GetWindowHandle();
	if (!windowHandle)
	{
		return result;
	}

	// Fetch a handle to this library.
	HMODULE moduleHandle = GetLibraryModuleHandle();

	// Hunt down a multisample format that is supported by this machine's video hardware.
	for (int sampleCount = 4; sampleCount > 0; sampleCount--)
	{
		// Create a temporary text label control.
		// We'll use it to bind a new OpenGL context to it below for testing purposes.
		auto controlHandle = ::CreateWindowEx(
				0, L"STATIC", nullptr, WS_CHILD, 0, 0, 0, 0, windowHandle, nullptr, moduleHandle, nullptr);
		if (!controlHandle)
		{
			return result;
		}

		// Fetch the temporary control's device context.
		HDC deviceContextHandle = ::GetDC(controlHandle);
		if (!deviceContextHandle)
		{
			::DestroyWindow(controlHandle);
			return result;
		}

		// Assign the below pixel format to the device context.
		PIXELFORMATDESCRIPTOR pixelFormatDescriptor {};
		pixelFormatDescriptor.nSize = sizeof(PIXELFORMATDESCRIPTOR);
		pixelFormatDescriptor.nVersion = 1;
		pixelFormatDescriptor.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
		pixelFormatDescriptor.iPixelType = PFD_TYPE_RGBA;
		pixelFormatDescriptor.cColorBits = 24;
		pixelFormatDescriptor.cDepthBits = 16;
		pixelFormatDescriptor.iLayerType = PFD_MAIN_PLANE;
		int pixelFormatIndex = ::ChoosePixelFormat(deviceContextHandle, &pixelFormatDescriptor);
		if (0 == pixelFormatIndex)
		{
			::ReleaseDC(controlHandle, deviceContextHandle);
			::DestroyWindow(controlHandle);
			return result;
		}
		BOOL wasFormatSet = ::SetPixelFormat(deviceContextHandle, pixelFormatIndex, &pixelFormatDescriptor);
		if (!wasFormatSet)
		{
			::ReleaseDC(controlHandle, deviceContextHandle);
			::DestroyWindow(controlHandle);
			return result;
		}

		// Create an OpenGL rendering context.
		HGLRC renderingContextHandle = ::wglCreateContext(deviceContextHandle);
		if (!renderingContextHandle)
		{
			::ReleaseDC(controlHandle, deviceContextHandle);
			::DestroyWindow(controlHandle);
			return result;
		}
// STEVE CHANGE TODO
		// Select the newly created OpenGL context.
		BOOL wasContextSelected = ::wglMakeCurrent(deviceContextHandle, renderingContextHandle);
		if (!wasContextSelected)
		{
			::wglDeleteContext(renderingContextHandle);
			::ReleaseDC(controlHandle, deviceContextHandle);
			::DestroyWindow(controlHandle);
			return result;
		}

		// Load OpenGL extensions.
		if (glewInit() != GLEW_OK)
		{
			::wglDeleteContext(renderingContextHandle);
			::ReleaseDC(controlHandle, deviceContextHandle);
			::DestroyWindow(controlHandle);
			return result;
		}

		// Test for multisampling support.
		if (WGLEW_ARB_pixel_format && GLEW_ARB_multisample)
		{
			UINT formatCount = 0;
			int PFAttribs[] =
			{
				WGL_DRAW_TO_WINDOW_ARB, GL_TRUE,
				WGL_SUPPORT_OPENGL_ARB, GL_TRUE,
				WGL_DOUBLE_BUFFER_ARB, GL_TRUE,
				WGL_PIXEL_TYPE_ARB, WGL_TYPE_RGBA_ARB,
				WGL_COLOR_BITS_ARB, 24,
				WGL_DEPTH_BITS_ARB, 16,
				WGL_ACCELERATION_ARB, WGL_FULL_ACCELERATION_ARB,
				WGL_SAMPLE_BUFFERS_ARB, GL_TRUE,
				WGL_SAMPLES_ARB, sampleCount,
				0
			};
			pixelFormatIndex = 0;
			BOOL wasPixelFormatChosen = ::wglChoosePixelFormatARB(
					deviceContextHandle, PFAttribs, NULL, 1, &pixelFormatIndex, &formatCount);
			if (wasPixelFormatChosen && (formatCount > 0))
			{
				result.IsSupported = true;
				result.PixelFormatIndex = pixelFormatIndex;
			}
		}

		// Destroy the temporary control and OpenGL context.
		::wglDeleteContext(renderingContextHandle);
		::ReleaseDC(controlHandle, deviceContextHandle);
		::DestroyWindow(controlHandle);
// /STEVE CHANGE
		// Stop now if multisampling is supported.
		if (result.IsSupported)
		{
			break;
		}
	}

	// Return the final result.
	return result;
}

void RenderSurfaceControl::OnReceivedMessage(UIComponent& sender, HandleMessageEventArgs& arguments)
{
	switch (arguments.GetMessageId())
	{
		case WM_ERASEBKGND:
		{
			// As an optimization, always handle the "Erase Background" message so that the operating system
			// won't automatically paint over the background. We'll just let OpenGL paint over the entire surface.
			// Handle the 
			arguments.SetHandled();
			arguments.SetReturnResult(1);
			break;
		}
		case WM_PAINT:
		{
			// Fetch this paint request's device context in case it is not targeting the display/monitor.
			// For example, it could reference a printer device context.
			PAINTSTRUCT paintStruct{};
			HWND windowHandle = GetWindowHandle();
			fPaintDeviceContextHandle = ::BeginPaint(windowHandle, &paintStruct);
			bool hasPaintDeviceContext = (fPaintDeviceContextHandle != nullptr);

			// Request the owner of this control to paint its content.
			OnPaint();

			// Release the BeginPaint() function's device context, if received.
			if (hasPaintDeviceContext)
			{
				::EndPaint(windowHandle, &paintStruct);
				fPaintDeviceContextHandle = nullptr;
			}

			// Flag that we've painted to the control's entire region.
			// Note: Windows will keep sending this control paint messages until we've flagged it as validated.
			ValidateRect(windowHandle, nullptr);

			// Flag that the paint message has been handled.
			arguments.SetHandled();
			arguments.SetReturnResult(0);
			break;
		}
	}
}

void RenderSurfaceControl::OnPaint()
{
	// Fetch this control's window handle.
	auto windowHandle = GetWindowHandle();
	if (!windowHandle)
	{
		return;
	}

	// Render to the control.
	if (fMainDeviceContextHandle && fRenderingContextHandle)
	{
		// Select this control's OpenGL context.
		SelectRenderingContext();

		// If the owner of this surface has provided a RenderFrameHandler, then use it to draw the next frame.
		// Otherwise, draw a black screen until a handler has been given to this surface.
		bool didDraw = false;
		if (fRenderFrameEventHandlerPointer)
		{
			HandledEventArgs arguments;
			try
			{
				fRenderFrameEventHandlerPointer->Invoke(*this, arguments);
				didDraw = arguments.WasHandled();
			}
			catch (std::exception ex) { }
		}
		if (false == didDraw)
		{
// STEVE CHANGE TODO
			::glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
			::glClear(GL_COLOR_BUFFER_BIT);
// /STEVE CHANGE
			SwapBuffers();
		}
	}
	else
	{
		// We were unable to set up an OpenGL context.
		// Render an error message to the control via GDI instead.
	}
}

#pragma endregion


#pragma region Version Class
RenderSurfaceControl::Version::Version()
:	Version(0, 0)
{
}

RenderSurfaceControl::Version::Version(int majorNumber, int minorNumber)
:	fMajorNumber(majorNumber),
	fMinorNumber(minorNumber)
{
	fVersionString = std::make_shared<std::string>("");
}

const char* RenderSurfaceControl::Version::GetString() const
{
	if (fVersionString)
	{
		return fVersionString->c_str();
	}
	return nullptr;
}

void RenderSurfaceControl::Version::SetString(const char* value)
{
	if (value)
	{
		fVersionString = std::make_shared<std::string>(value);
	}
	else
	{
		fVersionString = nullptr;
	}
}

int RenderSurfaceControl::Version::GetMajorNumber() const
{
	return fMajorNumber;
}

void RenderSurfaceControl::Version::SetMajorNumber(int value)
{
	fMajorNumber = value;
}

int RenderSurfaceControl::Version::GetMinorNumber() const
{
	return fMinorNumber;
}

void RenderSurfaceControl::Version::SetMinorNumber(int value)
{
	fMinorNumber = value;
}

int RenderSurfaceControl::Version::CompareTo(const RenderSurfaceControl::Version& version) const
{
	int x = (this->fMajorNumber * 100) + this->fMinorNumber;
	int y = (version.fMajorNumber * 100) + version.fMinorNumber;
	return x - y;
}

#pragma endregion

} }	// namespace Interop::UI
