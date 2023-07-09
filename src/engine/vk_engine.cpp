#include "vk_engine.h"

#include <triangle.frag.h>
#include <triangle.vert.h>
#include <vulkan/vulkan_core.h>

const int64_t one_second_ns = 1'000'000'000;

// Forward declarations
template <typename T>
const T &vkb_value_or_abort(vkb::Result<T> res);
void vk_check(VkResult err);

// public
void VulkanEngine::init() {
    // Get window
    SDL_InitSubSystem(SDL_INIT_VIDEO);
    m_window = SDL_CreateWindow("Learning Vulkan", SDL_WINDOWPOS_UNDEFINED,
                                SDL_WINDOWPOS_UNDEFINED, m_window_extent.width,
                                m_window_extent.height, SDL_WINDOW_VULKAN);

    // Get Vulkan instance
    m_instance = vkb_value_or_abort(vkb::InstanceBuilder{}
                                        .set_app_name("Learning Vulkan")
                                        .request_validation_layers()
                                        .require_api_version(vk_version)
                                        .use_default_debug_messenger()
                                        .build());

    // Get surface
    SDL_Vulkan_CreateSurface(m_window, m_instance, &m_surface);

    // Get device
    m_gpu = vkb_value_or_abort(
        vkb::PhysicalDeviceSelector{m_instance}
            .set_minimum_version(VK_API_VERSION_MAJOR(vk_version),
                                 VK_API_VERSION_MINOR(vk_version))
            .set_surface(m_surface)
            .select());

    m_device = vkb_value_or_abort(vkb::DeviceBuilder{m_gpu}.build());

    // Get Queues
    m_q_graphics =
        vkb_value_or_abort(m_device.get_queue(vkb::QueueType::graphics));
    m_qfamily_graphics =
        vkb_value_or_abort(m_device.get_queue_index(vkb::QueueType::graphics));

    // Get swapchain
    m_swapchain = vkb_value_or_abort(
        vkb::SwapchainBuilder{m_gpu, m_device, m_surface}
            .use_default_format_selection()
            .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
            .set_desired_extent(m_window_extent.width, m_window_extent.height)
            .build());
    m_swapchain_images = vkb_value_or_abort(m_swapchain.get_images());
    m_swapchain_views = vkb_value_or_abort(m_swapchain.get_image_views());

    // Get Command pool
    VkCommandPoolCreateInfo command_pool_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = m_qfamily_graphics,
    };
    vk_check(vkCreateCommandPool(m_device, &command_pool_info, nullptr,
                                 &m_cmd_pool));

    // Get Command buffer
    VkCommandBufferAllocateInfo cmdbuf_alloc_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = m_cmd_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    vk_check(
        vkAllocateCommandBuffers(m_device, &cmdbuf_alloc_info, &m_cmd_buf));

    // Get Render pass
    VkAttachmentDescription color_attachment{
        .format = m_swapchain.image_format,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
    };

    VkAttachmentReference color_attachment_ref{
        .attachment = 0,
        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };

    VkSubpassDescription subpass{
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,
        .pColorAttachments = &color_attachment_ref,
    };

    VkRenderPassCreateInfo render_pass_info{
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &color_attachment,
        .subpassCount = 1,
        .pSubpasses = &subpass,
    };
    vk_check(vkCreateRenderPass(m_device, &render_pass_info, nullptr,
                                &m_render_pass));

    // Get Frame buffer
    VkFramebufferCreateInfo fb_info{
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .pNext = nullptr,
        .renderPass = m_render_pass,
        .attachmentCount = 1,
        .width = m_window_extent.width,
        .height = m_window_extent.height,
        .layers = 1,
    };

    m_frame_bufs = std::vector<VkFramebuffer>{m_swapchain_images.size()};

    for (size_t i = 0; i < m_swapchain_images.size(); i++) {
        fb_info.pAttachments = &m_swapchain_views[i];
        vk_check(
            vkCreateFramebuffer(m_device, &fb_info, nullptr, &m_frame_bufs[i]));
    }

    // Create Synchronization structures
    VkFenceCreateInfo fence_info{
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    };
    vk_check(vkCreateFence(m_device, &fence_info, nullptr, &m_fence_render));

    VkSemaphoreCreateInfo semph_info{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    };
    vk_check(
        vkCreateSemaphore(m_device, &semph_info, nullptr, &m_semph_present));
    vk_check(
        vkCreateSemaphore(m_device, &semph_info, nullptr, &m_semph_render));

    // Load shaders -- VERSION FOR TESTING COMPILATION --
    VkShaderModule shader_triangle_vert;
    VkShaderModule shader_triangle_frag;

    load_shader(triangle_vert, sizeof(triangle_vert), &shader_triangle_vert);
    load_shader(triangle_frag, sizeof(triangle_frag), &shader_triangle_frag);

    vkDestroyShaderModule(m_device, shader_triangle_vert, nullptr);
    vkDestroyShaderModule(m_device, shader_triangle_frag, nullptr);

    // done
    m_initialized = true;
    printf("VulkanEngine::init OK\n");
}

void VulkanEngine::cleanup() {
    if (!m_initialized) {
        printf("VulkanEngine::cleanup: not initialized\n");
        return;
    }
    // Wait for the gpu to finish the pending work
    vk_check(
        vkWaitForFences(m_device, 1, &m_fence_render, true, one_second_ns));

    // Destroy in the inverse order of creation
    vkDestroySemaphore(m_device, m_semph_render, nullptr);
    vkDestroySemaphore(m_device, m_semph_present, nullptr);
    vkDestroyFence(m_device, m_fence_render, nullptr);
    vkDestroyCommandPool(m_device, m_cmd_pool, nullptr);
    vkb::destroy_swapchain(m_swapchain);
    vkDestroyRenderPass(m_device, m_render_pass, nullptr);
    for (size_t i = 0; i < m_frame_bufs.size(); i++) {
        vkDestroyFramebuffer(m_device, m_frame_bufs[i], nullptr);
        vkDestroyImageView(m_device, m_swapchain_views[i], nullptr);
    }
    vkb::destroy_device(m_device);
    vkb::destroy_surface(m_instance, m_surface);
    vkb::destroy_instance(m_instance);
    SDL_DestroyWindow(m_window);

    printf("VulkanEngine::cleanup OK\n");
}

void VulkanEngine::run() {
    SDL_Event event;

    while (true) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                goto run_QUIT;
            }
        }
        draw();
    }

run_QUIT:
    return;
}

void VulkanEngine::draw() {
    vk_check(
        vkWaitForFences(m_device, 1, &m_fence_render, true, one_second_ns));
    vk_check(vkResetFences(m_device, 1, &m_fence_render));

    uint32_t swap_img_idx;
    vk_check(vkAcquireNextImageKHR(m_device, m_swapchain, one_second_ns,
                                   m_semph_present, nullptr, &swap_img_idx));

    // now that we are sure that the commands finished executing, we can safely
    // reset the command buffer to begin recording again.
    vk_check(vkResetCommandBuffer(m_cmd_buf, 0));

    VkCommandBufferBeginInfo cmd_begin_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vk_check(vkBeginCommandBuffer(m_cmd_buf, &cmd_begin_info));

    // make a clear-color from frame number. This will flash with a 120*pi frame
    // period.
    float flash = abs(sin(m_frame_count / 120.f));
    VkClearValue clear_value{
        .color = {.float32{0.0f, 0.0f, flash, 1.0f}},
    };

    // start the main renderpass.
    VkRenderPassBeginInfo renderpass_begin_info{
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = m_render_pass,
        .framebuffer = m_frame_bufs[swap_img_idx],
        .renderArea{
            .extent = m_window_extent,
        },
        .clearValueCount = 1,
        .pClearValues = &clear_value,
    };
    vkCmdBeginRenderPass(m_cmd_buf, &renderpass_begin_info,
                         VK_SUBPASS_CONTENTS_INLINE);

    // TODO: render stuff here

    // finalize the render pass and the command buffer
    vkCmdEndRenderPass(m_cmd_buf);
    vk_check(vkEndCommandBuffer(m_cmd_buf));

    // prepare the submission to the queue.
    VkPipelineStageFlags wait_stage =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &m_semph_present,
        .pWaitDstStageMask = &wait_stage,  // you'll learn about this later
        .commandBufferCount = 1,
        .pCommandBuffers = &m_cmd_buf,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &m_semph_render,
    };
    vk_check(vkQueueSubmit(m_q_graphics, 1, &submit, m_fence_render));

    VkPresentInfoKHR present_info{
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &m_semph_render,
        .swapchainCount = 1,
        .pSwapchains = &m_swapchain.swapchain,
        .pImageIndices = &swap_img_idx,
    };
    vk_check(vkQueuePresentKHR(m_q_graphics, &present_info));

    m_frame_count++;
}

bool VulkanEngine::load_shader(const uint32_t buffer[], size_t size,
                               VkShaderModule *out) {
    VkShaderModuleCreateInfo createInfo{
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = size,
        .pCode = buffer,
    };

    vk_check(vkCreateShaderModule(m_device, &createInfo, nullptr, out));
    return true;
}

void vk_check(VkResult err) {
    if (err) {
        printf("Detected Vulkan error: %d\n", err);
        abort();
    }
}

template <typename T>
const T &vkb_value_or_abort(vkb::Result<T> res) {
    if (!res.has_value()) {
        printf("Detected VKB error: %s\n",
               res.full_error().type.message().c_str());
        abort();
    }
    return res.value();
}
