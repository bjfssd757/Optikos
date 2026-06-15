#ifndef VULKANRENDERER_H
#define VULKANRENDERER_H

#include <vulkan/vulkan.h>

#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "platform/IWindow.hpp"
#include "render/IRenderer.hpp"
#include "render/RenderQueue.hpp"
#include "shader/IShader.hpp"
#include "utilities/logger.hpp"

namespace Optikos
{
int constexpr DEFAULT_SHADER = 0;

class VulkanRenderer : public IRenderer
{
   public:
    explicit VulkanRenderer(IWindow* window, std::unique_ptr<IShader> shader);
    ~VulkanRenderer() override;

    void         onWindowResize(int width, int height) override;
    void         beginFrame() override;
    void         endFrame() override;
    void         submit(const DrawCommand&& command) override;
    void         flush() override;
    void         swap_buffer() override;
    unsigned int loadTexture(const std::vector<unsigned char>& data, int width,
                             int height) override;

    void resetToDefault() override;
    void restoreStates() override;

    IRenderQueue& getRenderQueue() override;

   private:
    IWindow*                 m_window;
    std::unique_ptr<IShader> m_shader;
    RenderQueue              m_renderQueue;

    VkInstance               m_instance       = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;

    std::unordered_map<std::string, unsigned int> m_shaderCache;
    unsigned int                                  m_defaultShader = DEFAULT_SHADER;

    std::unordered_map<std::string, unsigned int> m_textureCache;

    bool m_uiStateSet = false;

    static VKAPI_ATTR VkBool32 VKAPI_CALL
    debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT      messageSeverity,
                  VkDebugUtilsMessageTypeFlagsEXT             messageType,
                  const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData);

    VkResult CreateDebugUtilsMessengerEXT(VkInstance                                instance,
                                          const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
                                          const VkAllocationCallbacks*              pAllocator,
                                          VkDebugUtilsMessengerEXT* pDebugMessenger);

    void DestroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger,
                                       const VkAllocationCallbacks* pAllocator);

    void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo);
};

}  // namespace Optikos

#endif /* VULKANRENDERER_H */