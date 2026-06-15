#include "VulkanRenderer.hpp"

namespace Optikos
{

VulkanRenderer::VulkanRenderer(IWindow* window, std::unique_ptr<IShader> shader)
    : m_window(window), m_shader(std::move(shader))
{
    VkApplicationInfo appInfo{};
    appInfo.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName   = APP_NAME;
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName        = ENGINE_NAME;
    appInfo.engineVersion      = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion         = VK_API_VERSION_1_3;

    LOG_INFO("Vulkan version: " + std::to_string(VK_API_VERSION_1_3), "log");

    auto extensions = m_window->getVulkanExtensions();

    VkInstanceCreateInfo createInfo{};
    createInfo.sType            = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;

#ifdef __APPLE__
    createInfo.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
    extensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
#endif

#ifdef ENABLE_VULKAN_DEBUG_LAYER
    extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif

    createInfo.enabledExtensionCount   = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();
    createInfo.enabledLayerCount       = 0;

    uint32_t layerCount = 0;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

    std::vector<VkLayerProperties> availableLayers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

    LOG_INFO("Available Vulkan layers on this system:", "log");
    for (const auto& layer : availableLayers) LOG_INFO(layer.layerName, "log");
    LOG_INFO("END", "log");

    createInfo.enabledLayerCount = 0;

    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};

#ifdef ENABLE_VULKAN_DEBUG_LAYER
    const char* validationLayers[] = {"VK_LAYER_KHRONOS_validation"};
    createInfo.enabledLayerCount   = 1;
    createInfo.ppEnabledLayerNames = validationLayers;
    LOG_INFO("[VulkanRenderer] Validation layer ENABLED", "log");

    populateDebugMessengerCreateInfo(debugCreateInfo);
    createInfo.pNext = &debugCreateInfo;
#else
    LOG_INFO("[VulkanRenderer] Validation layer DISABLED", "log");
    createInfo.pNext = nullptr;
#endif

    VkResult result = vkCreateInstance(&createInfo, nullptr, &m_instance);

    if (result != VK_SUCCESS)
    {
        LOG_ERROR(
            "[VulkanRenderer] vkCreateInstance failed! Result code: " + std::to_string(result),
            "log");
        throw std::runtime_error("Failed to create Vulkan instance");
    }

#ifdef ENABLE_VULKAN_DEBUG_LAYER
    if (CreateDebugUtilsMessengerEXT(m_instance, &debugCreateInfo, nullptr, &m_debugMessenger) !=
        VK_SUCCESS)
    {
        LOG_ERROR(
            "[VulkanRenderer] CreateDebugUtilsMessengerEXT failed! Result code: " + std::to_string(result),
            "log");
        throw std::runtime_error("failed to set up debug messenger!");
    }
#endif

    uint32_t extensionCount = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);

    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, availableExtensions.data());

    LOG_INFO("Available Vulkan extensions on this system:", "log");
    for (const auto& extension : availableExtensions) LOG_INFO(extension.extensionName, "log");
    LOG_INFO("END", "log");
}

VulkanRenderer::~VulkanRenderer()
{
    if (m_debugMessenger) DestroyDebugUtilsMessengerEXT(m_instance, m_debugMessenger, nullptr);
    if (m_instance) vkDestroyInstance(m_instance, nullptr);
}

void VulkanRenderer::onWindowResize(int width, int height)
{
    (void) width;
    (void) height;
}

void VulkanRenderer::beginFrame()
{
}

void VulkanRenderer::endFrame()
{
}

void VulkanRenderer::submit(const DrawCommand&& command)
{
    (void) command;
}

void VulkanRenderer::flush()
{
}

void VulkanRenderer::swap_buffer()
{
}

unsigned int VulkanRenderer::loadTexture(const std::vector<unsigned char>& data, int width,
                                         int height)
{
    (void) data;
    (void) width;
    (void) height;
    return 0;
}

void VulkanRenderer::resetToDefault()
{
}

void VulkanRenderer::restoreStates()
{
}

IRenderQueue& VulkanRenderer::getRenderQueue()
{
    return m_renderQueue;
}

VKAPI_ATTR VkBool32 VKAPI_CALL VulkanRenderer::debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT      messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT             messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData)
{
    (void) messageType;
    (void) pUserData;
    std::string msg = std::string("[Validation Layer] ") + pCallbackData->pMessage;

    if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        LOG_ERROR(msg, "log");
    else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        LOG_WARN(msg, "log");
    else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT)
        LOG_INFO(msg, "log");
    else
        LOG_TRACE(msg, "log");

    return VK_FALSE;
}

VkResult VulkanRenderer::CreateDebugUtilsMessengerEXT(
    VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
    const VkAllocationCallbacks* pAllocator, VkDebugUtilsMessengerEXT* pDebugMessenger)
{
    auto func = (PFN_vkCreateDebugUtilsMessengerEXT) vkGetInstanceProcAddr(
        instance, "vkCreateDebugUtilsMessengerEXT");
    if (func != nullptr)
    {
        return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
    }
    else
    {
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
}

void VulkanRenderer::DestroyDebugUtilsMessengerEXT(VkInstance                   instance,
                                                   VkDebugUtilsMessengerEXT     debugMessenger,
                                                   const VkAllocationCallbacks* pAllocator)
{
    auto func = (PFN_vkDestroyDebugUtilsMessengerEXT) vkGetInstanceProcAddr(
        instance, "vkDestroyDebugUtilsMessengerEXT");
    if (func != nullptr)
    {
        func(instance, debugMessenger, pAllocator);
    }
}

void VulkanRenderer::populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo) {
    createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = debugCallback;
    createInfo.pUserData = nullptr;
}

}  // namespace Optikos