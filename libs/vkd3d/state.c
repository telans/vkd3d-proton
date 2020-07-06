/*
 * Copyright 2016 Józef Kucia for CodeWeavers
 * Copyright 2016 Henri Verbeet for CodeWeavers
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include "vkd3d_private.h"

/* ID3D12RootSignature */
static inline struct d3d12_root_signature *impl_from_ID3D12RootSignature(ID3D12RootSignature *iface)
{
    return CONTAINING_RECORD(iface, struct d3d12_root_signature, ID3D12RootSignature_iface);
}

static HRESULT STDMETHODCALLTYPE d3d12_root_signature_QueryInterface(ID3D12RootSignature *iface,
        REFIID riid, void **object)
{
    TRACE("iface %p, riid %s, object %p.\n", iface, debugstr_guid(riid), object);

    if (IsEqualGUID(riid, &IID_ID3D12RootSignature)
            || IsEqualGUID(riid, &IID_ID3D12DeviceChild)
            || IsEqualGUID(riid, &IID_ID3D12Object)
            || IsEqualGUID(riid, &IID_IUnknown))
    {
        ID3D12RootSignature_AddRef(iface);
        *object = iface;
        return S_OK;
    }

    WARN("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(riid));

    *object = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE d3d12_root_signature_AddRef(ID3D12RootSignature *iface)
{
    struct d3d12_root_signature *root_signature = impl_from_ID3D12RootSignature(iface);
    ULONG refcount = InterlockedIncrement(&root_signature->refcount);

    TRACE("%p increasing refcount to %u.\n", root_signature, refcount);

    return refcount;
}

static void d3d12_root_signature_cleanup(struct d3d12_root_signature *root_signature,
        struct d3d12_device *device)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    unsigned int i;

    VK_CALL(vkDestroyPipelineLayout(device->vk_device, root_signature->vk_pipeline_layout, NULL));
    VK_CALL(vkDestroyDescriptorSetLayout(device->vk_device, root_signature->vk_sampler_descriptor_layout, NULL));
    VK_CALL(vkDestroyDescriptorSetLayout(device->vk_device, root_signature->vk_packed_descriptor_layout, NULL));
    VK_CALL(vkDestroyDescriptorSetLayout(device->vk_device, root_signature->vk_root_descriptor_layout, NULL));

    for (i = 0; i < root_signature->static_sampler_count; ++i)
        VK_CALL(vkDestroySampler(device->vk_device, root_signature->static_samplers[i], NULL));

    vkd3d_free(root_signature->parameters);
    vkd3d_free(root_signature->bindings);
    vkd3d_free(root_signature->root_constants);
    vkd3d_free(root_signature->static_samplers);
}

static ULONG STDMETHODCALLTYPE d3d12_root_signature_Release(ID3D12RootSignature *iface)
{
    struct d3d12_root_signature *root_signature = impl_from_ID3D12RootSignature(iface);
    ULONG refcount = InterlockedDecrement(&root_signature->refcount);

    TRACE("%p decreasing refcount to %u.\n", root_signature, refcount);

    if (!refcount)
    {
        struct d3d12_device *device = root_signature->device;
        vkd3d_private_store_destroy(&root_signature->private_store);
        d3d12_root_signature_cleanup(root_signature, device);
        vkd3d_free(root_signature);
        d3d12_device_release(device);
    }

    return refcount;
}

static HRESULT STDMETHODCALLTYPE d3d12_root_signature_GetPrivateData(ID3D12RootSignature *iface,
        REFGUID guid, UINT *data_size, void *data)
{
    struct d3d12_root_signature *root_signature = impl_from_ID3D12RootSignature(iface);

    TRACE("iface %p, guid %s, data_size %p, data %p.\n", iface, debugstr_guid(guid), data_size, data);

    return vkd3d_get_private_data(&root_signature->private_store, guid, data_size, data);
}

static HRESULT STDMETHODCALLTYPE d3d12_root_signature_SetPrivateData(ID3D12RootSignature *iface,
        REFGUID guid, UINT data_size, const void *data)
{
    struct d3d12_root_signature *root_signature = impl_from_ID3D12RootSignature(iface);

    TRACE("iface %p, guid %s, data_size %u, data %p.\n", iface, debugstr_guid(guid), data_size, data);

    return vkd3d_set_private_data(&root_signature->private_store, guid, data_size, data);
}

static HRESULT STDMETHODCALLTYPE d3d12_root_signature_SetPrivateDataInterface(ID3D12RootSignature *iface,
        REFGUID guid, const IUnknown *data)
{
    struct d3d12_root_signature *root_signature = impl_from_ID3D12RootSignature(iface);

    TRACE("iface %p, guid %s, data %p.\n", iface, debugstr_guid(guid), data);

    return vkd3d_set_private_data_interface(&root_signature->private_store, guid, data);
}

static HRESULT STDMETHODCALLTYPE d3d12_root_signature_SetName(ID3D12RootSignature *iface, const WCHAR *name)
{
    struct d3d12_root_signature *root_signature = impl_from_ID3D12RootSignature(iface);

    TRACE("iface %p, name %s.\n", iface, debugstr_w(name, root_signature->device->wchar_size));

    return name ? S_OK : E_INVALIDARG;
}

static HRESULT STDMETHODCALLTYPE d3d12_root_signature_GetDevice(ID3D12RootSignature *iface,
        REFIID iid, void **device)
{
    struct d3d12_root_signature *root_signature = impl_from_ID3D12RootSignature(iface);

    TRACE("iface %p, iid %s, device %p.\n", iface, debugstr_guid(iid), device);

    return d3d12_device_query_interface(root_signature->device, iid, device);
}

static CONST_VTBL struct ID3D12RootSignatureVtbl d3d12_root_signature_vtbl =
{
    /* IUnknown methods */
    d3d12_root_signature_QueryInterface,
    d3d12_root_signature_AddRef,
    d3d12_root_signature_Release,
    /* ID3D12Object methods */
    d3d12_root_signature_GetPrivateData,
    d3d12_root_signature_SetPrivateData,
    d3d12_root_signature_SetPrivateDataInterface,
    d3d12_root_signature_SetName,
    /* ID3D12DeviceChild methods */
    d3d12_root_signature_GetDevice,
};

struct d3d12_root_signature *unsafe_impl_from_ID3D12RootSignature(ID3D12RootSignature *iface)
{
    if (!iface)
        return NULL;
    assert(iface->lpVtbl == &d3d12_root_signature_vtbl);
    return impl_from_ID3D12RootSignature(iface);
}

static VkShaderStageFlags stage_flags_from_visibility(D3D12_SHADER_VISIBILITY visibility)
{
    switch (visibility)
    {
        case D3D12_SHADER_VISIBILITY_ALL:
            return VK_SHADER_STAGE_ALL;
        case D3D12_SHADER_VISIBILITY_VERTEX:
            return VK_SHADER_STAGE_VERTEX_BIT;
        case D3D12_SHADER_VISIBILITY_HULL:
            return VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
        case D3D12_SHADER_VISIBILITY_DOMAIN:
            return VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
        case D3D12_SHADER_VISIBILITY_GEOMETRY:
            return VK_SHADER_STAGE_GEOMETRY_BIT;
        case D3D12_SHADER_VISIBILITY_PIXEL:
            return VK_SHADER_STAGE_FRAGMENT_BIT;
        default:
            return 0;
    }
}

static enum vkd3d_shader_visibility vkd3d_shader_visibility_from_d3d12(D3D12_SHADER_VISIBILITY visibility)
{
    switch (visibility)
    {
        case D3D12_SHADER_VISIBILITY_ALL:
            return VKD3D_SHADER_VISIBILITY_ALL;
        case D3D12_SHADER_VISIBILITY_VERTEX:
            return VKD3D_SHADER_VISIBILITY_VERTEX;
        case D3D12_SHADER_VISIBILITY_HULL:
            return VKD3D_SHADER_VISIBILITY_HULL;
        case D3D12_SHADER_VISIBILITY_DOMAIN:
            return VKD3D_SHADER_VISIBILITY_DOMAIN;
        case D3D12_SHADER_VISIBILITY_GEOMETRY:
            return VKD3D_SHADER_VISIBILITY_GEOMETRY;
        case D3D12_SHADER_VISIBILITY_PIXEL:
            return VKD3D_SHADER_VISIBILITY_PIXEL;
        default:
            FIXME("Unhandled visibility %#x.\n", visibility);
            return VKD3D_SHADER_VISIBILITY_ALL;
    }
}

static VkDescriptorType vk_descriptor_type_from_d3d12_range_type(const struct vkd3d_bindless_state *bindless_state,
        D3D12_DESCRIPTOR_RANGE_TYPE type, bool is_buffer)
{
    switch (type)
    {
        case D3D12_DESCRIPTOR_RANGE_TYPE_SRV:
            return is_buffer ? VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER : VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        case D3D12_DESCRIPTOR_RANGE_TYPE_UAV:
            return is_buffer ? VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        case D3D12_DESCRIPTOR_RANGE_TYPE_CBV:
            return vkd3d_bindless_state_get_cbv_descriptor_type(bindless_state);
        case D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER:
            return VK_DESCRIPTOR_TYPE_SAMPLER;
        default:
            FIXME("Unhandled descriptor range type type %#x.\n", type);
            return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    }
}

static VkDescriptorType vk_descriptor_type_from_d3d12_root_parameter(D3D12_ROOT_PARAMETER_TYPE type)
{
    switch (type)
    {
        /* SRV and UAV root parameters are buffer views. */
        case D3D12_ROOT_PARAMETER_TYPE_SRV:
            return VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
        case D3D12_ROOT_PARAMETER_TYPE_UAV:
            return VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
        case D3D12_ROOT_PARAMETER_TYPE_CBV:
            return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        default:
            FIXME("Unhandled descriptor root parameter type %#x.\n", type);
            return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    }
}

static enum vkd3d_shader_descriptor_type vkd3d_descriptor_type_from_d3d12_range_type(
        D3D12_DESCRIPTOR_RANGE_TYPE type)
{
    switch (type)
    {
        case D3D12_DESCRIPTOR_RANGE_TYPE_SRV:
            return VKD3D_SHADER_DESCRIPTOR_TYPE_SRV;
        case D3D12_DESCRIPTOR_RANGE_TYPE_UAV:
            return VKD3D_SHADER_DESCRIPTOR_TYPE_UAV;
        case D3D12_DESCRIPTOR_RANGE_TYPE_CBV:
            return VKD3D_SHADER_DESCRIPTOR_TYPE_CBV;
        case D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER:
            return VKD3D_SHADER_DESCRIPTOR_TYPE_SAMPLER;
        default:
            FIXME("Unhandled descriptor range type type %#x.\n", type);
            return VKD3D_SHADER_DESCRIPTOR_TYPE_SRV;
    }
}

static enum vkd3d_shader_descriptor_type vkd3d_descriptor_type_from_d3d12_root_parameter_type(
        D3D12_ROOT_PARAMETER_TYPE type)
{
    switch (type)
    {
        case D3D12_ROOT_PARAMETER_TYPE_SRV:
            return VKD3D_SHADER_DESCRIPTOR_TYPE_SRV;
        case D3D12_ROOT_PARAMETER_TYPE_UAV:
            return VKD3D_SHADER_DESCRIPTOR_TYPE_UAV;
        case D3D12_ROOT_PARAMETER_TYPE_CBV:
            return VKD3D_SHADER_DESCRIPTOR_TYPE_CBV;
        default:
            FIXME("Unhandled descriptor root parameter type %#x.\n", type);
            return VKD3D_SHADER_DESCRIPTOR_TYPE_SRV;
    }
}

static HRESULT vkd3d_create_descriptor_set_layout(struct d3d12_device *device,
        VkDescriptorSetLayoutCreateFlags flags, unsigned int binding_count,
        const VkDescriptorSetLayoutBinding *bindings, VkDescriptorSetLayout *set_layout, bool need_volatile_descriptors)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    VkDescriptorSetLayoutBindingFlagsCreateInfoEXT flags_info;
    VkDescriptorBindingFlagsEXT *binding_flags = NULL;
    VkDescriptorSetLayoutCreateInfo set_desc;
    unsigned int i;
    VkResult vr;

    set_desc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    set_desc.pNext = NULL;
    set_desc.flags = flags;
    set_desc.bindingCount = binding_count;
    set_desc.pBindings = bindings;

    if (need_volatile_descriptors && device->vk_info.supports_volatile_packed_descriptors)
    {
        set_desc.flags |= VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT_EXT;
        flags_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO_EXT;
        flags_info.pNext = NULL;
        flags_info.bindingCount = binding_count;
        binding_flags = vkd3d_malloc(sizeof(*binding_flags) * binding_count);
        if (!binding_flags)
        {
            ERR("Failed to allocate binding flags.\n");
            return hresult_from_vk_result(VK_ERROR_OUT_OF_HOST_MEMORY);
        }

        for (i = 0; i < binding_count; i++)
            binding_flags[i] = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT_EXT;

        flags_info.pBindingFlags = binding_flags;
        set_desc.pNext = &flags_info;
    }

    if ((vr = VK_CALL(vkCreateDescriptorSetLayout(device->vk_device, &set_desc, NULL, set_layout))) < 0)
    {
        WARN("Failed to create Vulkan descriptor set layout, vr %d.\n", vr);
        vkd3d_free(binding_flags);
        return hresult_from_vk_result(vr);
    }

    vkd3d_free(binding_flags);
    return S_OK;
}

static HRESULT vkd3d_create_pipeline_layout(struct d3d12_device *device,
        unsigned int set_layout_count, const VkDescriptorSetLayout *set_layouts,
        unsigned int push_constant_count, const VkPushConstantRange *push_constants,
        VkPipelineLayout *pipeline_layout)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    struct VkPipelineLayoutCreateInfo pipeline_layout_info;
    VkResult vr;

    pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_layout_info.pNext = NULL;
    pipeline_layout_info.flags = 0;
    pipeline_layout_info.setLayoutCount = set_layout_count;
    pipeline_layout_info.pSetLayouts = set_layouts;
    pipeline_layout_info.pushConstantRangeCount = push_constant_count;
    pipeline_layout_info.pPushConstantRanges = push_constants;
    if ((vr = VK_CALL(vkCreatePipelineLayout(device->vk_device,
            &pipeline_layout_info, NULL, pipeline_layout))) < 0)
    {
        WARN("Failed to create Vulkan pipeline layout, vr %d.\n", vr);
        return hresult_from_vk_result(vr);
    }

    return S_OK;
}

static bool d3d12_root_signature_descriptor_range_is_bindless(struct d3d12_device *device,
        D3D12_DESCRIPTOR_RANGE_TYPE range_type)
{
    uint32_t bindless_flags = 0;

    switch (range_type)
    {
        case D3D12_DESCRIPTOR_RANGE_TYPE_CBV:
            bindless_flags = VKD3D_BINDLESS_CBV;
            break;

        case D3D12_DESCRIPTOR_RANGE_TYPE_SRV:
            bindless_flags = VKD3D_BINDLESS_SRV;
            break;

        case D3D12_DESCRIPTOR_RANGE_TYPE_UAV:
            bindless_flags = VKD3D_BINDLESS_UAV;
            break;

        case D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER:
            bindless_flags = VKD3D_BINDLESS_SAMPLER;
            break;
    }

    return !!(device->bindless_state.flags & bindless_flags);
}

struct d3d12_root_signature_info
{
    uint32_t binding_count;
    uint32_t descriptor_count;

    uint32_t push_descriptor_count;
    uint32_t root_constant_count;
    bool has_uav_counters;

    uint32_t cost;
};

static HRESULT d3d12_root_signature_info_count_descriptors(struct d3d12_root_signature_info *info,
        struct d3d12_device *device, const D3D12_DESCRIPTOR_RANGE *range)
{
    size_t descriptor_count;

    if (d3d12_root_signature_descriptor_range_is_bindless(device, range->RangeType))
    {
        switch (range->RangeType)
        {
            case D3D12_DESCRIPTOR_RANGE_TYPE_SRV:
                /* separate image + buffer descriptors */
                info->binding_count += 2;
                break;
            case D3D12_DESCRIPTOR_RANGE_TYPE_UAV:
                /* separate image + buffer descriptors */
                info->binding_count += 2;

                if (device->bindless_state.flags & VKD3D_BINDLESS_UAV_COUNTER)
                {
                    info->binding_count += 1;
                    info->has_uav_counters = true;
                }
                else if (range->NumDescriptors != 0xffffffffu)
                {
                    info->binding_count += range->NumDescriptors;
                    info->descriptor_count += range->NumDescriptors;
                }
                break;
            case D3D12_DESCRIPTOR_RANGE_TYPE_CBV:
            case D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER:
                info->binding_count += 1;
                break;
            default:
                FIXME("Unhandled descriptor type %#x.\n", range->RangeType);
                return E_NOTIMPL;
        }
    }
    else
    {
        if (range->NumDescriptors == 0xffffffff)
        {
            FIXME("Unhandled unbound descriptor range.\n");
            return E_NOTIMPL;
        }

        switch (range->RangeType)
        {
            case D3D12_DESCRIPTOR_RANGE_TYPE_SRV:
                /* separate image + buffer descriptors */
                descriptor_count = range->NumDescriptors * 2;
                break;
            case D3D12_DESCRIPTOR_RANGE_TYPE_UAV:
                /* separate image + buffer descriptors */
                descriptor_count = range->NumDescriptors * 2;

                if (device->bindless_state.flags & VKD3D_BINDLESS_UAV_COUNTER)
                {
                    info->binding_count += 1;
                    info->has_uav_counters = true;
                }
                else
                    descriptor_count += range->NumDescriptors;
                break;
            case D3D12_DESCRIPTOR_RANGE_TYPE_CBV:
            case D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER:
                descriptor_count = range->NumDescriptors;
                break;
            default:
                FIXME("Unhandled descriptor type %#x.\n", range->RangeType);
                return E_NOTIMPL;
        }

        info->binding_count += descriptor_count;
        info->descriptor_count += descriptor_count;
    }

    return S_OK;
}

static HRESULT d3d12_root_signature_info_from_desc(struct d3d12_root_signature_info *info,
        struct d3d12_device *device, const D3D12_ROOT_SIGNATURE_DESC *desc)
{
    unsigned int i, j;
    HRESULT hr;

    memset(info, 0, sizeof(*info));

    for (i = 0; i < desc->NumParameters; ++i)
    {
        const D3D12_ROOT_PARAMETER *p = &desc->pParameters[i];

        switch (p->ParameterType)
        {
            case D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE:
                for (j = 0; j < p->DescriptorTable.NumDescriptorRanges; ++j)
                    if (FAILED(hr = d3d12_root_signature_info_count_descriptors(info,
                            device, &p->DescriptorTable.pDescriptorRanges[j])))
                        return hr;
                info->cost += 1;
                break;

            case D3D12_ROOT_PARAMETER_TYPE_CBV:
            case D3D12_ROOT_PARAMETER_TYPE_SRV:
            case D3D12_ROOT_PARAMETER_TYPE_UAV:
                info->binding_count += 1;
                info->push_descriptor_count += 1;
                info->cost += 2;
                break;

            case D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS:
                info->root_constant_count += 1;
                info->cost += p->Constants.Num32BitValues;
                break;

            default:
                FIXME("Unhandled type %#x for parameter %u.\n", p->ParameterType, i);
                return E_NOTIMPL;
        }
    }

    info->binding_count += desc->NumStaticSamplers;

    /* Account for UAV counter binding */
    if (info->has_uav_counters)
        info->push_descriptor_count += 1;
    return S_OK;
}

static HRESULT d3d12_root_signature_init_push_constants(struct d3d12_root_signature *root_signature,
        const D3D12_ROOT_SIGNATURE_DESC *desc, const struct d3d12_root_signature_info *info,
        struct VkPushConstantRange *push_constant_range)
{
    unsigned int i, j;

    push_constant_range->stageFlags = VK_SHADER_STAGE_ALL;
    push_constant_range->offset = 0;
    push_constant_range->size = 0;

    for (i = 0, j = 0; i < desc->NumParameters; ++i)
    {
        const D3D12_ROOT_PARAMETER *p = &desc->pParameters[i];

        if (p->ParameterType != D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS)
            continue;

        assert(p->ShaderVisibility <= D3D12_SHADER_VISIBILITY_PIXEL);
        root_signature->root_constant_mask |= 1ull << i;

        root_signature->parameters[i].parameter_type = p->ParameterType;
        root_signature->parameters[i].constant.constant_index = push_constant_range->size / sizeof(uint32_t);
        root_signature->parameters[i].constant.constant_count = p->Constants.Num32BitValues;

        root_signature->root_constants[j].register_space = p->Constants.RegisterSpace;
        root_signature->root_constants[j].register_index = p->Constants.ShaderRegister;
        root_signature->root_constants[j].shader_visibility = vkd3d_shader_visibility_from_d3d12(p->ShaderVisibility);
        root_signature->root_constants[j].offset = push_constant_range->size;
        root_signature->root_constants[j].size = p->Constants.Num32BitValues * sizeof(uint32_t);

        push_constant_range->size += p->Constants.Num32BitValues * sizeof(uint32_t);

        ++j;
    }

    /* Append one 32-bit push constant for each descriptor table offset */
    if (root_signature->device->bindless_state.flags)
    {
        root_signature->descriptor_table_offset = push_constant_range->size;

        for (i = 0; i < desc->NumParameters; ++i)
        {
            const D3D12_ROOT_PARAMETER *p = &desc->pParameters[i];

            if (p->ParameterType != D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE)
                continue;

            root_signature->descriptor_table_count += 1;
            push_constant_range->size += sizeof(uint32_t);
        }
    }

    return S_OK;
}

struct vkd3d_descriptor_set_context
{
    uint32_t packed_descriptor_index;
    uint32_t binding_index;
    uint32_t vk_set;
    uint32_t vk_binding;
};

static HRESULT d3d12_root_signature_init_root_descriptor_tables(struct d3d12_root_signature *root_signature,
        const D3D12_ROOT_SIGNATURE_DESC *desc, const struct d3d12_root_signature_info *info,
        struct vkd3d_descriptor_set_context *context, VkDescriptorSetLayout *vk_set_layout)
{
    struct vkd3d_bindless_state *bindless_state = &root_signature->device->bindless_state;
    VkDescriptorSetLayoutBinding *vk_binding_info;
    struct vkd3d_shader_resource_binding binding;
    struct d3d12_root_descriptor_table *table;
    uint32_t range_descriptor_offset;
    unsigned int i, j, k, d, t, range_count;
    HRESULT hr;

    if (!(vk_binding_info = vkd3d_malloc(info->descriptor_count * sizeof(*vk_binding_info))))
        return E_OUTOFMEMORY;

    for (i = 0, t = 0, d = 0; i < desc->NumParameters; ++i)
    {
        const D3D12_ROOT_PARAMETER *p = &desc->pParameters[i];
        if (p->ParameterType != D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE)
            continue;

        root_signature->descriptor_table_mask |= 1ull << i;

        table = &root_signature->parameters[i].descriptor_table;
        range_count = p->DescriptorTable.NumDescriptorRanges;
        range_descriptor_offset = 0;

        root_signature->parameters[i].parameter_type = p->ParameterType;

        table->table_index = t++;
        table->binding_count = 0;
        table->first_binding = &root_signature->bindings[context->binding_index];
        table->first_packed_descriptor = context->packed_descriptor_index;
        table->flags = 0;

        for (j = 0; j < range_count; ++j)
        {
            const D3D12_DESCRIPTOR_RANGE *range = &p->DescriptorTable.pDescriptorRanges[j];

            bool is_srv = range->RangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            bool is_uav = range->RangeType == D3D12_DESCRIPTOR_RANGE_TYPE_UAV;

            if (range->OffsetInDescriptorsFromTableStart != D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND)
                range_descriptor_offset = range->OffsetInDescriptorsFromTableStart;

            binding.type = vkd3d_descriptor_type_from_d3d12_range_type(range->RangeType);
            binding.register_space = range->RegisterSpace;
            binding.register_index = range->BaseShaderRegister;
            binding.register_count = range->NumDescriptors;
            binding.descriptor_table = table->table_index;
            binding.descriptor_offset = range_descriptor_offset;
            binding.shader_visibility = vkd3d_shader_visibility_from_d3d12(p->ShaderVisibility);

            if (d3d12_root_signature_descriptor_range_is_bindless(root_signature->device, range->RangeType))
            {
                if (vkd3d_bindless_state_find_binding(bindless_state, range->RangeType, VKD3D_SHADER_BINDING_FLAG_BUFFER, &binding.binding))
                {
                    binding.flags = VKD3D_SHADER_BINDING_FLAG_BINDLESS | VKD3D_SHADER_BINDING_FLAG_BUFFER;
                    table->first_binding[table->binding_count++] = binding;
                }

                if (vkd3d_bindless_state_find_binding(bindless_state, range->RangeType, VKD3D_SHADER_BINDING_FLAG_IMAGE, &binding.binding))
                {
                    binding.flags = VKD3D_SHADER_BINDING_FLAG_BINDLESS | VKD3D_SHADER_BINDING_FLAG_IMAGE;
                    table->first_binding[table->binding_count++] = binding;
                }
            }
            else
            {
                table->flags |= VKD3D_ROOT_DESCRIPTOR_TABLE_HAS_PACKED_DESCRIPTORS;

                binding.binding.set = context->vk_set;
                binding.binding.binding = context->vk_binding;

                /* Unroll descriptor range. */
                for (k = 0; k < range->NumDescriptors; ++k)
                {
                    VkDescriptorSetLayoutBinding vk_binding;
                    vk_binding.binding = binding.binding.binding = context->vk_binding++;
                    vk_binding.descriptorType = vk_descriptor_type_from_d3d12_range_type(bindless_state, range->RangeType, true);
                    vk_binding.descriptorCount = 1;
                    vk_binding.stageFlags = stage_flags_from_visibility(p->ShaderVisibility);
                    vk_binding.pImmutableSamplers = NULL;

                    binding.register_index = range->BaseShaderRegister + k;
                    binding.register_count = 1;
                    binding.descriptor_offset = range_descriptor_offset + k;
                    binding.flags = range->RangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER
                            ? VKD3D_SHADER_BINDING_FLAG_IMAGE
                            : VKD3D_SHADER_BINDING_FLAG_BUFFER;

                    table->first_binding[table->binding_count++] = binding;
                    vk_binding_info[d++] = vk_binding;

                    /* Add image binding for SRV/UAV */
                    if (is_srv || is_uav)
                    {
                        vk_binding.binding = binding.binding.binding = context->vk_binding++;
                        vk_binding.descriptorType = vk_descriptor_type_from_d3d12_range_type(bindless_state, range->RangeType, false);
                        binding.flags = VKD3D_SHADER_BINDING_FLAG_IMAGE;

                        table->first_binding[table->binding_count++] = binding;
                        vk_binding_info[d++] = vk_binding;
                    }
                }
            }

            /* Add UAV counter bindings */
            if (is_uav)
            {
                if (root_signature->flags & VKD3D_ROOT_SIGNATURE_USE_BINDLESS_UAV_COUNTERS)
                {
                    binding.register_index = range->BaseShaderRegister;
                    binding.register_count = range->NumDescriptors;
                    binding.descriptor_offset = range_descriptor_offset;
                    binding.binding = root_signature->uav_counter_binding;
                    binding.flags = VKD3D_SHADER_BINDING_FLAG_BINDLESS | VKD3D_SHADER_BINDING_FLAG_COUNTER;

                    table->first_binding[table->binding_count++] = binding;
                }
                else if (range->NumDescriptors != 0xffffffffu)
                {
                    table->flags |= VKD3D_ROOT_DESCRIPTOR_TABLE_HAS_PACKED_DESCRIPTORS;

                    for (k = 0; k < range->NumDescriptors; ++k)
                    {
                        VkDescriptorSetLayoutBinding vk_binding;
                        vk_binding.binding = binding.binding.binding = context->vk_binding++;
                        vk_binding.descriptorType = vk_descriptor_type_from_d3d12_range_type(bindless_state, range->RangeType, true);
                        vk_binding.descriptorCount = 1;
                        vk_binding.stageFlags = stage_flags_from_visibility(p->ShaderVisibility);
                        vk_binding.pImmutableSamplers = NULL;

                        binding.binding.set = context->vk_set;
                        binding.register_index = range->BaseShaderRegister + k;
                        binding.register_count = 1;
                        binding.descriptor_offset = range_descriptor_offset + k;
                        binding.flags = VKD3D_SHADER_BINDING_FLAG_COUNTER;

                        table->first_binding[table->binding_count++] = binding;
                        vk_binding_info[d++] = vk_binding;
                    }
                }
                else
                    WARN("Unbounded UAV counter range not supported.\n");
            }

            range_descriptor_offset = binding.descriptor_offset + binding.register_count;
        }

        for (j = 0; j < table->binding_count; ++j)
        {
            if (!(table->first_binding[j].flags & VKD3D_SHADER_BINDING_FLAG_BINDLESS))
                context->packed_descriptor_index += table->first_binding[j].register_count;
        }

        context->binding_index += table->binding_count;
    }

    if (info->descriptor_count)
        hr = vkd3d_create_descriptor_set_layout(root_signature->device, 0,
                info->descriptor_count, vk_binding_info, vk_set_layout, true);
    else
        hr = S_OK;

    vkd3d_free(vk_binding_info);
    return hr;
}

static HRESULT d3d12_root_signature_init_root_descriptors(struct d3d12_root_signature *root_signature,
        const D3D12_ROOT_SIGNATURE_DESC *desc, const struct d3d12_root_signature_info *info,
        const VkPushConstantRange *push_constant_range, struct vkd3d_descriptor_set_context *context,
        VkDescriptorSetLayout *vk_set_layout)
{
    VkDescriptorSetLayoutBinding *vk_binding_info, *vk_binding;
    struct vkd3d_shader_resource_binding *binding;
    VkDescriptorSetLayoutCreateFlags vk_flags;
    struct d3d12_root_parameter *param;
    unsigned int i, j, rs_flags;
    HRESULT hr;

    rs_flags = VKD3D_ROOT_SIGNATURE_USE_INLINE_UNIFORM_BLOCK |
        VKD3D_ROOT_SIGNATURE_USE_BINDLESS_UAV_COUNTERS;

    if (!info->push_descriptor_count && !(root_signature->flags & rs_flags))
        return S_OK;

    if (!(vk_binding_info = vkd3d_malloc(sizeof(*vk_binding_info) * (info->push_descriptor_count + 1))))
        return E_OUTOFMEMORY;

    for (i = 0, j = 0; i < desc->NumParameters; ++i)
    {
        const D3D12_ROOT_PARAMETER *p = &desc->pParameters[i];
        if (p->ParameterType != D3D12_ROOT_PARAMETER_TYPE_CBV
                && p->ParameterType != D3D12_ROOT_PARAMETER_TYPE_SRV
                && p->ParameterType != D3D12_ROOT_PARAMETER_TYPE_UAV)
            continue;

        root_signature->root_descriptor_mask |= 1ull << i;

        vk_binding = &vk_binding_info[j++];
        vk_binding->binding = context->vk_binding;
        vk_binding->descriptorType = vk_descriptor_type_from_d3d12_root_parameter(p->ParameterType);
        vk_binding->descriptorCount = 1;
        vk_binding->stageFlags = stage_flags_from_visibility(p->ShaderVisibility);
        vk_binding->pImmutableSamplers = NULL;

        binding = &root_signature->bindings[context->binding_index];
        binding->type = vkd3d_descriptor_type_from_d3d12_root_parameter_type(p->ParameterType);
        binding->register_space = p->Descriptor.RegisterSpace;
        binding->register_index = p->Descriptor.ShaderRegister;
        binding->register_count = 1;
        binding->descriptor_table = 0;  /* ignored */
        binding->descriptor_offset = 0; /* ignored */
        binding->shader_visibility = vkd3d_shader_visibility_from_d3d12(p->ShaderVisibility);
        binding->flags = VKD3D_SHADER_BINDING_FLAG_BUFFER;
        binding->binding.binding = context->vk_binding;
        binding->binding.set = context->vk_set;

        param = &root_signature->parameters[i];
        param->parameter_type = p->ParameterType;
        param->descriptor.binding = binding;
        param->descriptor.packed_descriptor = context->packed_descriptor_index;

        context->packed_descriptor_index += 1;
        context->binding_index += 1;
        context->vk_binding += 1;
    }

    if (root_signature->flags & VKD3D_ROOT_SIGNATURE_USE_BINDLESS_UAV_COUNTERS)
    {
        vk_binding = &vk_binding_info[j++];
        vk_binding->binding = context->vk_binding;
        vk_binding->descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        vk_binding->descriptorCount = 1;
        vk_binding->stageFlags = VK_SHADER_STAGE_ALL;
        vk_binding->pImmutableSamplers = NULL;

        root_signature->uav_counter_binding.set = context->vk_set;
        root_signature->uav_counter_binding.binding = context->vk_binding;

        context->vk_binding += 1;
    }

    if (root_signature->flags & VKD3D_ROOT_SIGNATURE_USE_INLINE_UNIFORM_BLOCK)
    {
        vk_binding = &vk_binding_info[j++];
        vk_binding->binding = context->vk_binding;
        vk_binding->descriptorType = VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK_EXT;
        vk_binding->descriptorCount = push_constant_range->size;
        vk_binding->stageFlags = VK_SHADER_STAGE_ALL;
        vk_binding->pImmutableSamplers = NULL;

        root_signature->push_constant_ubo_binding.set = context->vk_set;
        root_signature->push_constant_ubo_binding.binding = context->vk_binding;

        context->vk_binding += 1;
    }

    vk_flags = root_signature->flags & VKD3D_ROOT_SIGNATURE_USE_PUSH_DESCRIPTORS
            ? VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR : 0;

    hr = vkd3d_create_descriptor_set_layout(root_signature->device, vk_flags,
            j, vk_binding_info, vk_set_layout, false);

    vkd3d_free(vk_binding_info);
    return hr;
}

static HRESULT d3d12_root_signature_init_static_samplers(struct d3d12_root_signature *root_signature,
        const D3D12_ROOT_SIGNATURE_DESC *desc, struct vkd3d_descriptor_set_context *context,
        VkDescriptorSetLayout *vk_set_layout)
{
    VkDescriptorSetLayoutBinding *vk_binding_info, *vk_binding;
    struct vkd3d_shader_resource_binding *binding;
    unsigned int i;
    HRESULT hr;

    if (!desc->NumStaticSamplers)
        return S_OK;

    if (!(vk_binding_info = malloc(desc->NumStaticSamplers * sizeof(*vk_binding_info))))
        return E_OUTOFMEMORY;

    for (i = 0; i < desc->NumStaticSamplers; ++i)
    {
        const D3D12_STATIC_SAMPLER_DESC *s = &desc->pStaticSamplers[i];

        if (FAILED(hr = d3d12_create_static_sampler(root_signature->device, s, &root_signature->static_samplers[i])))
            goto cleanup;

        vk_binding = &vk_binding_info[i];
        vk_binding->binding = context->vk_binding;
        vk_binding->descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
        vk_binding->descriptorCount = 1;
        vk_binding->stageFlags = stage_flags_from_visibility(s->ShaderVisibility);
        vk_binding->pImmutableSamplers = &root_signature->static_samplers[i];

        binding = &root_signature->bindings[context->binding_index];
        binding->type = VKD3D_SHADER_DESCRIPTOR_TYPE_SAMPLER;
        binding->register_space = s->RegisterSpace;
        binding->register_index = s->ShaderRegister;
        binding->register_count = 1;
        binding->descriptor_table = 0;  /* ignored */
        binding->descriptor_offset = 0; /* ignored */
        binding->shader_visibility = vkd3d_shader_visibility_from_d3d12(s->ShaderVisibility);
        binding->flags = VKD3D_SHADER_BINDING_FLAG_IMAGE;
        binding->binding.binding = context->vk_binding;
        binding->binding.set = context->vk_set;

        context->binding_index += 1;
        context->vk_binding += 1;
    }

    hr = vkd3d_create_descriptor_set_layout(root_signature->device, 0,
            desc->NumStaticSamplers, vk_binding_info, vk_set_layout, false);

cleanup:
    vkd3d_free(vk_binding_info);
    return hr;
}

static HRESULT d3d12_root_signature_init(struct d3d12_root_signature *root_signature,
        struct d3d12_device *device, const D3D12_ROOT_SIGNATURE_DESC *desc)
{
    const VkPhysicalDeviceProperties *vk_device_properties = &device->device_info.properties2.properties;
    const struct vkd3d_bindless_state *bindless_state = &device->bindless_state;
    VkDescriptorSetLayout set_layouts[VKD3D_MAX_DESCRIPTOR_SETS];
    struct vkd3d_descriptor_set_context context;
    unsigned int i, push_constant_range_count;
    struct d3d12_root_signature_info info;
    HRESULT hr;

    memset(&context, 0, sizeof(context));
    memset(root_signature, 0, sizeof(*root_signature));
    root_signature->ID3D12RootSignature_iface.lpVtbl = &d3d12_root_signature_vtbl;
    root_signature->refcount = 1;

    root_signature->d3d12_flags = desc->Flags;

    if (desc->Flags & ~(D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
            | D3D12_ROOT_SIGNATURE_FLAG_ALLOW_STREAM_OUTPUT))
        FIXME("Ignoring root signature flags %#x.\n", desc->Flags);

    if (FAILED(hr = d3d12_root_signature_info_from_desc(&info, device, desc)))
        return hr;

    if (info.cost > D3D12_MAX_ROOT_COST)
    {
        WARN("Root signature cost %u exceeds maximum allowed cost.\n", info.cost);
        return E_INVALIDARG;
    }

    /* needed by some methods, increment ref count later */
    root_signature->device = device;
    root_signature->binding_count = info.binding_count;
    root_signature->static_sampler_count = desc->NumStaticSamplers;
    root_signature->packed_descriptor_count = info.descriptor_count;

    hr = E_OUTOFMEMORY;
    root_signature->parameter_count = desc->NumParameters;
    if (!(root_signature->parameters = vkd3d_calloc(root_signature->parameter_count,
            sizeof(*root_signature->parameters))))
        goto fail;
    if (!(root_signature->bindings = vkd3d_calloc(root_signature->binding_count,
            sizeof(*root_signature->bindings))))
        goto fail;
    root_signature->root_constant_count = info.root_constant_count;
    if (!(root_signature->root_constants = vkd3d_calloc(root_signature->root_constant_count,
            sizeof(*root_signature->root_constants))))
        goto fail;
    if (!(root_signature->static_samplers = vkd3d_calloc(root_signature->static_sampler_count,
            sizeof(*root_signature->static_samplers))))
        goto fail;

    for (i = 0; i < bindless_state->set_count; i++)
        set_layouts[context.vk_set++] = bindless_state->set_info[i].vk_set_layout;

    if (FAILED(hr = d3d12_root_signature_init_static_samplers(root_signature, desc,
                &context, &root_signature->vk_sampler_descriptor_layout)))
        goto fail;

    if (root_signature->vk_sampler_descriptor_layout)
    {
        set_layouts[context.vk_set] = root_signature->vk_sampler_descriptor_layout;
        root_signature->sampler_descriptor_set = context.vk_set;

        context.packed_descriptor_index = 0;
        context.vk_binding = 0;
        context.vk_set += 1;
    }

    if (FAILED(hr = d3d12_root_signature_init_push_constants(root_signature, desc, &info,
            &root_signature->push_constant_range)))
        goto fail;

    if (root_signature->push_constant_range.size <= vk_device_properties->limits.maxPushConstantsSize)
    {
        if (info.push_descriptor_count <= device->device_info.push_descriptor_properties.maxPushDescriptors)
            root_signature->flags |= VKD3D_ROOT_SIGNATURE_USE_PUSH_DESCRIPTORS;
    }
    else if (device->device_info.inline_uniform_block_features.inlineUniformBlock)
    {
        /* Stores push constant data with the root descriptor set,
         * so we can't use push descriptors in this case. */
        root_signature->flags |= VKD3D_ROOT_SIGNATURE_USE_INLINE_UNIFORM_BLOCK;
    }
    else
    {
        ERR("Root signature requires %d bytes of push constant space, but device only supports %d bytes.\n",
                root_signature->push_constant_range.size, vk_device_properties->limits.maxPushConstantsSize);
        goto fail;
    }

    if (info.has_uav_counters)
        root_signature->flags |= VKD3D_ROOT_SIGNATURE_USE_BINDLESS_UAV_COUNTERS;

    if (FAILED(hr = d3d12_root_signature_init_root_descriptors(root_signature, desc,
                &info, &root_signature->push_constant_range, &context,
                &root_signature->vk_root_descriptor_layout)))
        goto fail;

    if (root_signature->vk_root_descriptor_layout)
    {
        set_layouts[context.vk_set] = root_signature->vk_root_descriptor_layout;
        root_signature->root_descriptor_set = context.vk_set;

        context.packed_descriptor_index = 0;
        context.vk_binding = 0;
        context.vk_set += 1;
    }

    if (FAILED(hr = d3d12_root_signature_init_root_descriptor_tables(root_signature, desc,
                &info, &context, &root_signature->vk_packed_descriptor_layout)))
        goto fail;

    if (root_signature->vk_packed_descriptor_layout)
    {
        root_signature->packed_descriptor_set = context.vk_set;
        set_layouts[context.vk_set] = root_signature->vk_packed_descriptor_layout;
        context.vk_set += 1;
    }

    push_constant_range_count = 0;

    if (root_signature->push_constant_range.size &&
            !(root_signature->flags & VKD3D_ROOT_SIGNATURE_USE_INLINE_UNIFORM_BLOCK))
        push_constant_range_count = 1;

    if (FAILED(hr = vkd3d_create_pipeline_layout(device, context.vk_set, set_layouts,
            push_constant_range_count, &root_signature->push_constant_range,
            &root_signature->vk_pipeline_layout)))
        goto fail;

    if (FAILED(hr = vkd3d_private_store_init(&root_signature->private_store)))
        goto fail;

    d3d12_device_add_ref(root_signature->device);
    return S_OK;

fail:
    d3d12_root_signature_cleanup(root_signature, device);
    return hr;
}

HRESULT d3d12_root_signature_create(struct d3d12_device *device,
        const void *bytecode, size_t bytecode_length, struct d3d12_root_signature **root_signature)
{
    const struct vkd3d_shader_code dxbc = {bytecode, bytecode_length};
    union
    {
        D3D12_VERSIONED_ROOT_SIGNATURE_DESC d3d12;
        struct vkd3d_versioned_root_signature_desc vkd3d;
    } root_signature_desc;
    struct d3d12_root_signature *object;
    HRESULT hr;
    int ret;

    if ((ret = vkd3d_parse_root_signature_v_1_0(&dxbc, &root_signature_desc.vkd3d)) < 0)
    {
        WARN("Failed to parse root signature, vkd3d result %d.\n", ret);
        return hresult_from_vkd3d_result(ret);
    }

    if (!(object = vkd3d_malloc(sizeof(*object))))
    {
        vkd3d_shader_free_root_signature(&root_signature_desc.vkd3d);
        return E_OUTOFMEMORY;
    }

    hr = d3d12_root_signature_init(object, device, &root_signature_desc.d3d12.Desc_1_0);
    vkd3d_shader_free_root_signature(&root_signature_desc.vkd3d);
    if (FAILED(hr))
    {
        vkd3d_free(object);
        return hr;
    }

    TRACE("Created root signature %p.\n", object);

    *root_signature = object;

    return S_OK;
}

static unsigned int d3d12_root_signature_get_shader_interface_flags(const struct d3d12_root_signature *root_signature)
{
    unsigned int flags = 0;

    if (root_signature->flags & VKD3D_ROOT_SIGNATURE_USE_INLINE_UNIFORM_BLOCK)
        flags |= VKD3D_SHADER_INTERFACE_PUSH_CONSTANTS_AS_UNIFORM_BUFFER;

    if (root_signature->device->bindless_state.flags & VKD3D_BINDLESS_CBV_AS_SSBO)
        flags |= VKD3D_SHADER_INTERFACE_BINDLESS_CBV_AS_STORAGE_BUFFER;

    return flags;
}

/* vkd3d_render_pass_cache */
struct vkd3d_render_pass_entry
{
    struct vkd3d_render_pass_key key;
    VkRenderPass vk_render_pass;
};

STATIC_ASSERT(sizeof(struct vkd3d_render_pass_key) == 48);

static VkImageLayout vkd3d_render_pass_get_depth_stencil_layout(const struct vkd3d_render_pass_key *key)
{
    if (!key->depth_enable && !key->stencil_enable)
        return VK_IMAGE_LAYOUT_UNDEFINED;

    if (key->depth_write && key->stencil_write)
        return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    else if (key->depth_write)
        return VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL;
    else if (key->stencil_write)
        return VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL;
    else
        return VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
}

static HRESULT vkd3d_render_pass_cache_create_pass_locked(struct vkd3d_render_pass_cache *cache,
        struct d3d12_device *device, const struct vkd3d_render_pass_key *key, VkRenderPass *vk_render_pass)
{
    VkAttachmentReference attachment_references[D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT + 1];
    VkAttachmentDescription attachments[D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT + 1];
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    struct vkd3d_render_pass_entry *entry;
    unsigned int index, attachment_index;
    VkSubpassDependency dependencies[2];
    VkSubpassDescription sub_pass_desc;
    VkRenderPassCreateInfo pass_info;
    VkPipelineStageFlags stages;
    bool have_depth_stencil;
    unsigned int rt_count;
    VkResult vr;

    if (!vkd3d_array_reserve((void **)&cache->render_passes, &cache->render_passes_size,
            cache->render_pass_count + 1, sizeof(*cache->render_passes)))
    {
        *vk_render_pass = VK_NULL_HANDLE;
        return E_OUTOFMEMORY;
    }

    entry = &cache->render_passes[cache->render_pass_count];

    entry->key = *key;

    have_depth_stencil = key->depth_enable || key->stencil_enable;
    rt_count = have_depth_stencil ? key->attachment_count - 1 : key->attachment_count;
    assert(rt_count <= D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT);

    stages = 0;

    for (index = 0, attachment_index = 0; index < rt_count; ++index)
    {
        if (!key->vk_formats[index])
        {
            attachment_references[index].attachment = VK_ATTACHMENT_UNUSED;
            attachment_references[index].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            continue;
        }

        attachments[attachment_index].flags = 0;
        attachments[attachment_index].format = key->vk_formats[index];
        attachments[attachment_index].samples = key->sample_count;
        attachments[attachment_index].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        attachments[attachment_index].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[attachment_index].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[attachment_index].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[attachment_index].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        attachments[attachment_index].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        attachment_references[index].attachment = attachment_index;
        attachment_references[index].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        stages |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        ++attachment_index;
    }

    if (have_depth_stencil)
    {
        VkImageLayout depth_layout = vkd3d_render_pass_get_depth_stencil_layout(key);

        attachments[attachment_index].flags = 0;
        attachments[attachment_index].format = key->vk_formats[index];
        attachments[attachment_index].samples = key->sample_count;
        attachments[attachment_index].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        attachments[attachment_index].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[attachment_index].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        attachments[attachment_index].stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[attachment_index].initialLayout = depth_layout;
        attachments[attachment_index].finalLayout = depth_layout;

        attachment_references[index].attachment = attachment_index;
        attachment_references[index].layout = depth_layout;

        stages |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        attachment_index++;
    }

    /* HACK: Stage masks should technically not be 0 */
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = stages;
    dependencies[0].dstStageMask = stages;
    dependencies[0].srcAccessMask = 0;
    dependencies[0].dstAccessMask = 0;
    dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask = stages;
    dependencies[1].dstStageMask = stages;
    dependencies[1].srcAccessMask = 0;
    dependencies[1].dstAccessMask = 0;
    dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    sub_pass_desc.flags = 0;
    sub_pass_desc.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub_pass_desc.inputAttachmentCount = 0;
    sub_pass_desc.pInputAttachments = NULL;
    sub_pass_desc.colorAttachmentCount = rt_count;
    sub_pass_desc.pColorAttachments = attachment_references;
    sub_pass_desc.pResolveAttachments = NULL;
    if (have_depth_stencil)
        sub_pass_desc.pDepthStencilAttachment = &attachment_references[rt_count];
    else
        sub_pass_desc.pDepthStencilAttachment = NULL;
    sub_pass_desc.preserveAttachmentCount = 0;
    sub_pass_desc.pPreserveAttachments = NULL;

    pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    pass_info.pNext = NULL;
    pass_info.flags = 0;
    pass_info.attachmentCount = attachment_index;
    pass_info.pAttachments = attachments;
    pass_info.subpassCount = 1;
    pass_info.pSubpasses = &sub_pass_desc;
    pass_info.dependencyCount = ARRAY_SIZE(dependencies);
    pass_info.pDependencies = dependencies;
    if ((vr = VK_CALL(vkCreateRenderPass(device->vk_device, &pass_info, NULL, vk_render_pass))) >= 0)
    {
        entry->vk_render_pass = *vk_render_pass;
        ++cache->render_pass_count;
    }
    else
    {
        WARN("Failed to create Vulkan render pass, vr %d.\n", vr);
        *vk_render_pass = VK_NULL_HANDLE;
    }

    return hresult_from_vk_result(vr);
}

HRESULT vkd3d_render_pass_cache_find(struct vkd3d_render_pass_cache *cache,
        struct d3d12_device *device, const struct vkd3d_render_pass_key *key, VkRenderPass *vk_render_pass)
{
    bool found = false;
    HRESULT hr = S_OK;
    unsigned int i;
    int rc;

    if ((rc = pthread_mutex_lock(&device->mutex)))
    {
        ERR("Failed to lock mutex, error %d.\n", rc);
        *vk_render_pass = VK_NULL_HANDLE;
        return hresult_from_errno(rc);
    }

    for (i = 0; i < cache->render_pass_count; ++i)
    {
        struct vkd3d_render_pass_entry *current = &cache->render_passes[i];

        if (!memcmp(&current->key, key, sizeof(*key)))
        {
            *vk_render_pass = current->vk_render_pass;
            found = true;
            break;
        }
    }

    if (!found)
        hr = vkd3d_render_pass_cache_create_pass_locked(cache, device, key, vk_render_pass);

    pthread_mutex_unlock(&device->mutex);

    return hr;
}

void vkd3d_render_pass_cache_init(struct vkd3d_render_pass_cache *cache)
{
    cache->render_passes = NULL;
    cache->render_pass_count = 0;
    cache->render_passes_size = 0;
}

void vkd3d_render_pass_cache_cleanup(struct vkd3d_render_pass_cache *cache,
        struct d3d12_device *device)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    unsigned int i;

    for (i = 0; i < cache->render_pass_count; ++i)
    {
        struct vkd3d_render_pass_entry *current = &cache->render_passes[i];
        VK_CALL(vkDestroyRenderPass(device->vk_device, current->vk_render_pass, NULL));
    }

    vkd3d_free(cache->render_passes);
    cache->render_passes = NULL;
}

struct vkd3d_compiled_pipeline
{
    struct list entry;
    struct vkd3d_pipeline_key key;
    VkPipeline vk_pipeline;
    VkRenderPass vk_render_pass;
};

/* ID3D12PipelineState */
static inline struct d3d12_pipeline_state *impl_from_ID3D12PipelineState(ID3D12PipelineState *iface)
{
    return CONTAINING_RECORD(iface, struct d3d12_pipeline_state, ID3D12PipelineState_iface);
}

static HRESULT STDMETHODCALLTYPE d3d12_pipeline_state_QueryInterface(ID3D12PipelineState *iface,
        REFIID riid, void **object)
{
    TRACE("iface %p, riid %s, object %p.\n", iface, debugstr_guid(riid), object);

    if (IsEqualGUID(riid, &IID_ID3D12PipelineState)
            || IsEqualGUID(riid, &IID_ID3D12Pageable)
            || IsEqualGUID(riid, &IID_ID3D12DeviceChild)
            || IsEqualGUID(riid, &IID_ID3D12Object)
            || IsEqualGUID(riid, &IID_IUnknown))
    {
        ID3D12PipelineState_AddRef(iface);
        *object = iface;
        return S_OK;
    }

    WARN("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(riid));

    *object = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE d3d12_pipeline_state_AddRef(ID3D12PipelineState *iface)
{
    struct d3d12_pipeline_state *state = impl_from_ID3D12PipelineState(iface);
    ULONG refcount = InterlockedIncrement(&state->refcount);

    TRACE("%p increasing refcount to %u.\n", state, refcount);

    return refcount;
}

static void d3d12_pipeline_state_destroy_graphics(struct d3d12_pipeline_state *state,
        struct d3d12_device *device)
{
    struct d3d12_graphics_pipeline_state *graphics = &state->graphics;
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    struct vkd3d_compiled_pipeline *current, *e;
    unsigned int i;

    for (i = 0; i < graphics->stage_count; ++i)
    {
        VK_CALL(vkDestroyShaderModule(device->vk_device, graphics->stages[i].module, NULL));
    }

    LIST_FOR_EACH_ENTRY_SAFE(current, e, &graphics->compiled_fallback_pipelines, struct vkd3d_compiled_pipeline, entry)
    {
        VK_CALL(vkDestroyPipeline(device->vk_device, current->vk_pipeline, NULL));
        vkd3d_free(current);
    }

    VK_CALL(vkDestroyPipeline(device->vk_device, graphics->pipeline, NULL));
}

static ULONG STDMETHODCALLTYPE d3d12_pipeline_state_Release(ID3D12PipelineState *iface)
{
    struct d3d12_pipeline_state *state = impl_from_ID3D12PipelineState(iface);
    ULONG refcount = InterlockedDecrement(&state->refcount);

    TRACE("%p decreasing refcount to %u.\n", state, refcount);

    if (!refcount)
    {
        struct d3d12_device *device = state->device;
        const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;

        vkd3d_private_store_destroy(&state->private_store);

        if (d3d12_pipeline_state_is_graphics(state))
            d3d12_pipeline_state_destroy_graphics(state, device);
        else if (d3d12_pipeline_state_is_compute(state))
            VK_CALL(vkDestroyPipeline(device->vk_device, state->compute.vk_pipeline, NULL));

        vkd3d_free(state);

        d3d12_device_release(device);
    }

    return refcount;
}

static HRESULT STDMETHODCALLTYPE d3d12_pipeline_state_GetPrivateData(ID3D12PipelineState *iface,
        REFGUID guid, UINT *data_size, void *data)
{
    struct d3d12_pipeline_state *state = impl_from_ID3D12PipelineState(iface);

    TRACE("iface %p, guid %s, data_size %p, data %p.\n", iface, debugstr_guid(guid), data_size, data);

    return vkd3d_get_private_data(&state->private_store, guid, data_size, data);
}

static HRESULT STDMETHODCALLTYPE d3d12_pipeline_state_SetPrivateData(ID3D12PipelineState *iface,
        REFGUID guid, UINT data_size, const void *data)
{
    struct d3d12_pipeline_state *state = impl_from_ID3D12PipelineState(iface);

    TRACE("iface %p, guid %s, data_size %u, data %p.\n", iface, debugstr_guid(guid), data_size, data);

    return vkd3d_set_private_data(&state->private_store, guid, data_size, data);
}

static HRESULT STDMETHODCALLTYPE d3d12_pipeline_state_SetPrivateDataInterface(ID3D12PipelineState *iface,
        REFGUID guid, const IUnknown *data)
{
    struct d3d12_pipeline_state *state = impl_from_ID3D12PipelineState(iface);

    TRACE("iface %p, guid %s, data %p.\n", iface, debugstr_guid(guid), data);

    return vkd3d_set_private_data_interface(&state->private_store, guid, data);
}

static HRESULT STDMETHODCALLTYPE d3d12_pipeline_state_SetName(ID3D12PipelineState *iface, const WCHAR *name)
{
    struct d3d12_pipeline_state *state = impl_from_ID3D12PipelineState(iface);

    TRACE("iface %p, name %s.\n", iface, debugstr_w(name, state->device->wchar_size));

    if (d3d12_pipeline_state_is_compute(state))
    {
        return vkd3d_set_vk_object_name(state->device, (uint64_t)state->compute.vk_pipeline,
                VK_OBJECT_TYPE_PIPELINE, name);
    }

    return name ? S_OK : E_INVALIDARG;
}

static HRESULT STDMETHODCALLTYPE d3d12_pipeline_state_GetDevice(ID3D12PipelineState *iface,
        REFIID iid, void **device)
{
    struct d3d12_pipeline_state *state = impl_from_ID3D12PipelineState(iface);

    TRACE("iface %p, iid %s, device %p.\n", iface, debugstr_guid(iid), device);

    return d3d12_device_query_interface(state->device, iid, device);
}

static HRESULT STDMETHODCALLTYPE d3d12_pipeline_state_GetCachedBlob(ID3D12PipelineState *iface,
        ID3DBlob **blob)
{
    struct d3d_blob *blob_object;
    HRESULT hr;

    FIXME_ONCE("iface %p, blob %p semi-stub!\n", iface, blob);

    if (FAILED(hr = d3d_blob_create(NULL, 0, &blob_object)))
    {
        ERR("Failed to create blob, hr %#x.", hr);
        return hr;
    }

    *blob = &blob_object->ID3DBlob_iface;
    return S_OK;
}

static CONST_VTBL struct ID3D12PipelineStateVtbl d3d12_pipeline_state_vtbl =
{
    /* IUnknown methods */
    d3d12_pipeline_state_QueryInterface,
    d3d12_pipeline_state_AddRef,
    d3d12_pipeline_state_Release,
    /* ID3D12Object methods */
    d3d12_pipeline_state_GetPrivateData,
    d3d12_pipeline_state_SetPrivateData,
    d3d12_pipeline_state_SetPrivateDataInterface,
    d3d12_pipeline_state_SetName,
    /* ID3D12DeviceChild methods */
    d3d12_pipeline_state_GetDevice,
    /* ID3D12PipelineState methods */
    d3d12_pipeline_state_GetCachedBlob,
};

struct d3d12_pipeline_state *unsafe_impl_from_ID3D12PipelineState(ID3D12PipelineState *iface)
{
    if (!iface)
        return NULL;
    assert(iface->lpVtbl == &d3d12_pipeline_state_vtbl);
    return impl_from_ID3D12PipelineState(iface);
}

static HRESULT create_shader_stage(struct d3d12_device *device,
        struct VkPipelineShaderStageCreateInfo *stage_desc, enum VkShaderStageFlagBits stage,
        const D3D12_SHADER_BYTECODE *code, const struct vkd3d_shader_interface_info *shader_interface,
        const struct vkd3d_shader_compile_arguments *compile_args)
{
    struct vkd3d_shader_code dxbc = {code->pShaderBytecode, code->BytecodeLength};
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    struct VkShaderModuleCreateInfo shader_desc;
    struct vkd3d_shader_code spirv = {0};
    VkResult vr;
    int ret;

    stage_desc->sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage_desc->pNext = NULL;
    stage_desc->flags = 0;
    stage_desc->stage = stage;
    stage_desc->pName = "main";
    stage_desc->pSpecializationInfo = NULL;

    shader_desc.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shader_desc.pNext = NULL;
    shader_desc.flags = 0;

    if ((ret = vkd3d_shader_compile_dxbc(&dxbc, &spirv, 0, shader_interface, compile_args)) < 0)
    {
        WARN("Failed to compile shader, vkd3d result %d.\n", ret);
        return hresult_from_vkd3d_result(ret);
    }
    shader_desc.codeSize = spirv.size;
    shader_desc.pCode = spirv.code;

    vr = VK_CALL(vkCreateShaderModule(device->vk_device, &shader_desc, NULL, &stage_desc->module));
    vkd3d_shader_free_shader_code(&spirv);
    if (vr < 0)
    {
        WARN("Failed to create Vulkan shader module, vr %d.\n", vr);
        return hresult_from_vk_result(vr);
    }

    return S_OK;
}

static HRESULT vkd3d_create_compute_pipeline(struct d3d12_device *device,
        const D3D12_SHADER_BYTECODE *code, const struct vkd3d_shader_interface_info *shader_interface,
        VkPipelineLayout vk_pipeline_layout, VkPipeline *vk_pipeline)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    VkComputePipelineCreateInfo pipeline_info;
    VkResult vr;
    HRESULT hr;

    pipeline_info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipeline_info.pNext = NULL;
    pipeline_info.flags = 0;
    if (FAILED(hr = create_shader_stage(device, &pipeline_info.stage,
            VK_SHADER_STAGE_COMPUTE_BIT, code, shader_interface, NULL)))
        return hr;
    pipeline_info.layout = vk_pipeline_layout;
    pipeline_info.basePipelineHandle = VK_NULL_HANDLE;
    pipeline_info.basePipelineIndex = -1;

    vr = VK_CALL(vkCreateComputePipelines(device->vk_device,
            VK_NULL_HANDLE, 1, &pipeline_info, NULL, vk_pipeline));
    VK_CALL(vkDestroyShaderModule(device->vk_device, pipeline_info.stage.module, NULL));
    if (vr < 0)
    {
        WARN("Failed to create Vulkan compute pipeline, hr %#x.", hr);
        return hresult_from_vk_result(vr);
    }

    return S_OK;
}

static HRESULT d3d12_pipeline_state_init_compute(struct d3d12_pipeline_state *state,
        struct d3d12_device *device, const struct d3d12_pipeline_state_desc *desc)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    struct vkd3d_shader_interface_info shader_interface;
    const struct d3d12_root_signature *root_signature;
    HRESULT hr;

    state->ID3D12PipelineState_iface.lpVtbl = &d3d12_pipeline_state_vtbl;
    state->refcount = 1;

    if (!(root_signature = unsafe_impl_from_ID3D12RootSignature(desc->root_signature)))
    {
        WARN("Root signature is NULL.\n");
        return E_INVALIDARG;
    }

    shader_interface.type = VKD3D_SHADER_STRUCTURE_TYPE_SHADER_INTERFACE_INFO;
    shader_interface.next = NULL;
    shader_interface.flags = d3d12_root_signature_get_shader_interface_flags(root_signature);
    shader_interface.descriptor_tables.offset = root_signature->descriptor_table_offset;
    shader_interface.descriptor_tables.count = root_signature->descriptor_table_count;
    shader_interface.bindings = root_signature->bindings;
    shader_interface.binding_count = root_signature->binding_count;
    shader_interface.push_constant_buffers = root_signature->root_constants;
    shader_interface.push_constant_buffer_count = root_signature->root_constant_count;
    shader_interface.push_constant_ubo_binding = &root_signature->push_constant_ubo_binding;

    if (FAILED(hr = vkd3d_create_compute_pipeline(device, &desc->cs, &shader_interface,
            root_signature->vk_pipeline_layout, &state->compute.vk_pipeline)))
    {
        WARN("Failed to create Vulkan compute pipeline, hr %#x.\n", hr);
        return hr;
    }

    if (FAILED(hr = vkd3d_private_store_init(&state->private_store)))
    {
        VK_CALL(vkDestroyPipeline(device->vk_device, state->compute.vk_pipeline, NULL));
        return hr;
    }

    state->vk_bind_point = VK_PIPELINE_BIND_POINT_COMPUTE;
    d3d12_device_add_ref(state->device = device);

    return S_OK;
}

static enum VkPolygonMode vk_polygon_mode_from_d3d12(D3D12_FILL_MODE mode)
{
    switch (mode)
    {
        case D3D12_FILL_MODE_WIREFRAME:
            return VK_POLYGON_MODE_LINE;
        case D3D12_FILL_MODE_SOLID:
            return VK_POLYGON_MODE_FILL;
        default:
            FIXME("Unhandled fill mode %#x.\n", mode);
            return VK_POLYGON_MODE_FILL;
    }
}

static enum VkCullModeFlagBits vk_cull_mode_from_d3d12(D3D12_CULL_MODE mode)
{
    switch (mode)
    {
        case D3D12_CULL_MODE_NONE:
            return VK_CULL_MODE_NONE;
        case D3D12_CULL_MODE_FRONT:
            return VK_CULL_MODE_FRONT_BIT;
        case D3D12_CULL_MODE_BACK:
            return VK_CULL_MODE_BACK_BIT;
        default:
            FIXME("Unhandled cull mode %#x.\n", mode);
            return VK_CULL_MODE_NONE;
    }
}

static void rs_desc_from_d3d12(VkPipelineRasterizationStateCreateInfo *vk_desc,
        const D3D12_RASTERIZER_DESC *d3d12_desc)
{
    vk_desc->sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    vk_desc->pNext = NULL;
    vk_desc->flags = 0;
    vk_desc->depthClampEnable = !d3d12_desc->DepthClipEnable;
    vk_desc->rasterizerDiscardEnable = VK_FALSE;
    vk_desc->polygonMode = vk_polygon_mode_from_d3d12(d3d12_desc->FillMode);
    vk_desc->cullMode = vk_cull_mode_from_d3d12(d3d12_desc->CullMode);
    vk_desc->frontFace = d3d12_desc->FrontCounterClockwise ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE;
    vk_desc->depthBiasEnable = d3d12_desc->DepthBias || d3d12_desc->SlopeScaledDepthBias;
    vk_desc->depthBiasConstantFactor = d3d12_desc->DepthBias;
    vk_desc->depthBiasClamp = d3d12_desc->DepthBiasClamp;
    vk_desc->depthBiasSlopeFactor = d3d12_desc->SlopeScaledDepthBias;
    vk_desc->lineWidth = 1.0f;

    if (d3d12_desc->MultisampleEnable)
        FIXME_ONCE("Ignoring MultisampleEnable %#x.\n", d3d12_desc->MultisampleEnable);
    if (d3d12_desc->AntialiasedLineEnable)
        FIXME_ONCE("Ignoring AntialiasedLineEnable %#x.\n", d3d12_desc->AntialiasedLineEnable);
    if (d3d12_desc->ForcedSampleCount)
        FIXME("Ignoring ForcedSampleCount %#x.\n", d3d12_desc->ForcedSampleCount);
    if (d3d12_desc->ConservativeRaster)
        FIXME("Ignoring ConservativeRaster %#x.\n", d3d12_desc->ConservativeRaster);
}

static void rs_depth_clip_info_from_d3d12(VkPipelineRasterizationDepthClipStateCreateInfoEXT *depth_clip_info,
        VkPipelineRasterizationStateCreateInfo *vk_rs_desc, const D3D12_RASTERIZER_DESC *d3d12_desc)
{
    vk_rs_desc->depthClampEnable = VK_TRUE;

    depth_clip_info->sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_DEPTH_CLIP_STATE_CREATE_INFO_EXT;
    depth_clip_info->pNext = NULL;
    depth_clip_info->flags = 0;
    depth_clip_info->depthClipEnable = d3d12_desc->DepthClipEnable;

    vk_prepend_struct(vk_rs_desc, depth_clip_info);
}

static void rs_stream_info_from_d3d12(VkPipelineRasterizationStateStreamCreateInfoEXT *stream_info,
        VkPipelineRasterizationStateCreateInfo *vk_rs_desc, const D3D12_STREAM_OUTPUT_DESC *so_desc,
        const struct vkd3d_vulkan_info *vk_info)
{
    if (!so_desc->RasterizedStream || so_desc->RasterizedStream == D3D12_SO_NO_RASTERIZED_STREAM)
        return;

    if (!vk_info->rasterization_stream)
    {
        FIXME("Rasterization stream select is not supported by Vulkan implementation.\n");
        return;
    }

    stream_info->sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_STREAM_CREATE_INFO_EXT;
    stream_info->pNext = NULL;
    stream_info->flags = 0;
    stream_info->rasterizationStream = so_desc->RasterizedStream;

    vk_prepend_struct(vk_rs_desc, stream_info);
}

static enum VkStencilOp vk_stencil_op_from_d3d12(D3D12_STENCIL_OP op)
{
    switch (op)
    {
        case D3D12_STENCIL_OP_KEEP:
            return VK_STENCIL_OP_KEEP;
        case D3D12_STENCIL_OP_ZERO:
            return VK_STENCIL_OP_ZERO;
        case D3D12_STENCIL_OP_REPLACE:
            return VK_STENCIL_OP_REPLACE;
        case D3D12_STENCIL_OP_INCR_SAT:
            return VK_STENCIL_OP_INCREMENT_AND_CLAMP;
        case D3D12_STENCIL_OP_DECR_SAT:
            return VK_STENCIL_OP_DECREMENT_AND_CLAMP;
        case D3D12_STENCIL_OP_INVERT:
            return VK_STENCIL_OP_INVERT;
        case D3D12_STENCIL_OP_INCR:
            return VK_STENCIL_OP_INCREMENT_AND_WRAP;
        case D3D12_STENCIL_OP_DECR:
            return VK_STENCIL_OP_DECREMENT_AND_WRAP;
        default:
            FIXME("Unhandled stencil op %#x.\n", op);
            return VK_STENCIL_OP_KEEP;
    }
}

enum VkCompareOp vk_compare_op_from_d3d12(D3D12_COMPARISON_FUNC op)
{
    switch (op)
    {
        case D3D12_COMPARISON_FUNC_NEVER:
            return VK_COMPARE_OP_NEVER;
        case D3D12_COMPARISON_FUNC_LESS:
            return VK_COMPARE_OP_LESS;
        case D3D12_COMPARISON_FUNC_EQUAL:
            return VK_COMPARE_OP_EQUAL;
        case D3D12_COMPARISON_FUNC_LESS_EQUAL:
            return VK_COMPARE_OP_LESS_OR_EQUAL;
        case D3D12_COMPARISON_FUNC_GREATER:
            return VK_COMPARE_OP_GREATER;
        case D3D12_COMPARISON_FUNC_NOT_EQUAL:
            return VK_COMPARE_OP_NOT_EQUAL;
        case D3D12_COMPARISON_FUNC_GREATER_EQUAL:
            return VK_COMPARE_OP_GREATER_OR_EQUAL;
        case D3D12_COMPARISON_FUNC_ALWAYS:
            return VK_COMPARE_OP_ALWAYS;
        default:
            FIXME("Unhandled compare op %#x.\n", op);
            return VK_COMPARE_OP_NEVER;
    }
}

static void vk_stencil_op_state_from_d3d12(struct VkStencilOpState *vk_desc,
        const D3D12_DEPTH_STENCILOP_DESC *d3d12_desc, uint32_t compare_mask, uint32_t write_mask)
{
    vk_desc->failOp = vk_stencil_op_from_d3d12(d3d12_desc->StencilFailOp);
    vk_desc->passOp = vk_stencil_op_from_d3d12(d3d12_desc->StencilPassOp);
    vk_desc->depthFailOp = vk_stencil_op_from_d3d12(d3d12_desc->StencilDepthFailOp);
    vk_desc->compareOp = vk_compare_op_from_d3d12(d3d12_desc->StencilFunc);
    vk_desc->compareMask = compare_mask;
    vk_desc->writeMask = write_mask;
    /* The stencil reference value is a dynamic state. Set by OMSetStencilRef(). */
    vk_desc->reference = 0;
}

static void ds_desc_from_d3d12(struct VkPipelineDepthStencilStateCreateInfo *vk_desc,
        const D3D12_DEPTH_STENCIL_DESC1 *d3d12_desc)
{
    memset(vk_desc, 0, sizeof(*vk_desc));
    vk_desc->sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    vk_desc->pNext = NULL;
    vk_desc->flags = 0;
    if ((vk_desc->depthTestEnable = d3d12_desc->DepthEnable))
    {
        vk_desc->depthWriteEnable = d3d12_desc->DepthWriteMask & D3D12_DEPTH_WRITE_MASK_ALL;
        vk_desc->depthCompareOp = vk_compare_op_from_d3d12(d3d12_desc->DepthFunc);
    }
    else
    {
        vk_desc->depthWriteEnable = VK_FALSE;
        vk_desc->depthCompareOp = VK_COMPARE_OP_NEVER;
    }
    vk_desc->depthBoundsTestEnable = d3d12_desc->DepthBoundsTestEnable;
    if ((vk_desc->stencilTestEnable = d3d12_desc->StencilEnable))
    {
        vk_stencil_op_state_from_d3d12(&vk_desc->front, &d3d12_desc->FrontFace,
                d3d12_desc->StencilReadMask, d3d12_desc->StencilWriteMask);
        vk_stencil_op_state_from_d3d12(&vk_desc->back, &d3d12_desc->BackFace,
                d3d12_desc->StencilReadMask, d3d12_desc->StencilWriteMask);
    }
    else
    {
        memset(&vk_desc->front, 0, sizeof(vk_desc->front));
        memset(&vk_desc->back, 0, sizeof(vk_desc->back));
    }
    vk_desc->minDepthBounds = 0.0f;
    vk_desc->maxDepthBounds = 1.0f;
}

static enum VkBlendFactor vk_blend_factor_from_d3d12(D3D12_BLEND blend, bool alpha)
{
    switch (blend)
    {
        case D3D12_BLEND_ZERO:
            return VK_BLEND_FACTOR_ZERO;
        case D3D12_BLEND_ONE:
            return VK_BLEND_FACTOR_ONE;
        case D3D12_BLEND_SRC_COLOR:
            return VK_BLEND_FACTOR_SRC_COLOR;
        case D3D12_BLEND_INV_SRC_COLOR:
            return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
        case D3D12_BLEND_SRC_ALPHA:
            return VK_BLEND_FACTOR_SRC_ALPHA;
        case D3D12_BLEND_INV_SRC_ALPHA:
            return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        case D3D12_BLEND_DEST_ALPHA:
            return VK_BLEND_FACTOR_DST_ALPHA;
        case D3D12_BLEND_INV_DEST_ALPHA:
            return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
        case D3D12_BLEND_DEST_COLOR:
            return VK_BLEND_FACTOR_DST_COLOR;
        case D3D12_BLEND_INV_DEST_COLOR:
            return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
        case D3D12_BLEND_SRC_ALPHA_SAT:
            return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
        case D3D12_BLEND_BLEND_FACTOR:
            if (alpha)
                return VK_BLEND_FACTOR_CONSTANT_ALPHA;
            return VK_BLEND_FACTOR_CONSTANT_COLOR;
        case D3D12_BLEND_INV_BLEND_FACTOR:
            if (alpha)
                return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;
            return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
        case D3D12_BLEND_SRC1_COLOR:
            return VK_BLEND_FACTOR_SRC1_COLOR;
        case D3D12_BLEND_INV_SRC1_COLOR:
            return VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR;
        case D3D12_BLEND_SRC1_ALPHA:
            return VK_BLEND_FACTOR_SRC1_ALPHA;
        case D3D12_BLEND_INV_SRC1_ALPHA:
            return VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA;
        default:
            FIXME("Unhandled blend %#x.\n", blend);
            return VK_BLEND_FACTOR_ZERO;
    }
}

static enum VkBlendOp vk_blend_op_from_d3d12(D3D12_BLEND_OP op)
{
    switch (op)
    {
        case D3D12_BLEND_OP_ADD:
            return VK_BLEND_OP_ADD;
        case D3D12_BLEND_OP_SUBTRACT:
            return VK_BLEND_OP_SUBTRACT;
        case D3D12_BLEND_OP_REV_SUBTRACT:
            return VK_BLEND_OP_REVERSE_SUBTRACT;
        case D3D12_BLEND_OP_MIN:
            return VK_BLEND_OP_MIN;
        case D3D12_BLEND_OP_MAX:
            return VK_BLEND_OP_MAX;
        default:
            FIXME("Unhandled blend op %#x.\n", op);
            return VK_BLEND_OP_ADD;
    }
}

static void blend_attachment_from_d3d12(struct VkPipelineColorBlendAttachmentState *vk_desc,
        const D3D12_RENDER_TARGET_BLEND_DESC *d3d12_desc)
{
    if (d3d12_desc->BlendEnable)
    {
        vk_desc->blendEnable = VK_TRUE;
        vk_desc->srcColorBlendFactor = vk_blend_factor_from_d3d12(d3d12_desc->SrcBlend, false);
        vk_desc->dstColorBlendFactor = vk_blend_factor_from_d3d12(d3d12_desc->DestBlend, false);
        vk_desc->colorBlendOp = vk_blend_op_from_d3d12(d3d12_desc->BlendOp);
        vk_desc->srcAlphaBlendFactor = vk_blend_factor_from_d3d12(d3d12_desc->SrcBlendAlpha, true);
        vk_desc->dstAlphaBlendFactor = vk_blend_factor_from_d3d12(d3d12_desc->DestBlendAlpha, true);
        vk_desc->alphaBlendOp = vk_blend_op_from_d3d12(d3d12_desc->BlendOpAlpha);
    }
    else
    {
        memset(vk_desc, 0, sizeof(*vk_desc));
    }
    vk_desc->colorWriteMask = 0;
    if (d3d12_desc->RenderTargetWriteMask & D3D12_COLOR_WRITE_ENABLE_RED)
        vk_desc->colorWriteMask |= VK_COLOR_COMPONENT_R_BIT;
    if (d3d12_desc->RenderTargetWriteMask & D3D12_COLOR_WRITE_ENABLE_GREEN)
        vk_desc->colorWriteMask |= VK_COLOR_COMPONENT_G_BIT;
    if (d3d12_desc->RenderTargetWriteMask & D3D12_COLOR_WRITE_ENABLE_BLUE)
        vk_desc->colorWriteMask |= VK_COLOR_COMPONENT_B_BIT;
    if (d3d12_desc->RenderTargetWriteMask & D3D12_COLOR_WRITE_ENABLE_ALPHA)
        vk_desc->colorWriteMask |= VK_COLOR_COMPONENT_A_BIT;
}

static VkLogicOp vk_logic_op_from_d3d12(D3D12_LOGIC_OP op)
{
    switch (op)
    {
        case D3D12_LOGIC_OP_CLEAR:
            return VK_LOGIC_OP_CLEAR;
        case D3D12_LOGIC_OP_SET:
            return VK_LOGIC_OP_SET;
        case D3D12_LOGIC_OP_COPY:
            return VK_LOGIC_OP_COPY;
        case D3D12_LOGIC_OP_COPY_INVERTED:
            return VK_LOGIC_OP_COPY_INVERTED;
        case D3D12_LOGIC_OP_NOOP:
            return VK_LOGIC_OP_NO_OP;
        case D3D12_LOGIC_OP_INVERT:
            return VK_LOGIC_OP_INVERT;
        case D3D12_LOGIC_OP_AND:
            return VK_LOGIC_OP_AND;
        case D3D12_LOGIC_OP_NAND:
            return VK_LOGIC_OP_NAND;
        case D3D12_LOGIC_OP_OR:
            return VK_LOGIC_OP_OR;
        case D3D12_LOGIC_OP_NOR:
            return VK_LOGIC_OP_NOR;
        case D3D12_LOGIC_OP_XOR:
            return VK_LOGIC_OP_XOR;
        case D3D12_LOGIC_OP_EQUIV:
            return VK_LOGIC_OP_EQUIVALENT;
        case D3D12_LOGIC_OP_AND_REVERSE:
            return VK_LOGIC_OP_AND_REVERSE;
        case D3D12_LOGIC_OP_AND_INVERTED:
            return VK_LOGIC_OP_AND_INVERTED;
        case D3D12_LOGIC_OP_OR_REVERSE:
            return VK_LOGIC_OP_OR_REVERSE;
        case D3D12_LOGIC_OP_OR_INVERTED:
            return VK_LOGIC_OP_OR_INVERTED;
        default:
            FIXME("Unhandled logic op %#x.\n", op);
            return VK_LOGIC_OP_NO_OP;
    }
}

static void blend_desc_from_d3d12(VkPipelineColorBlendStateCreateInfo *vk_desc, const D3D12_BLEND_DESC *d3d12_desc,
        uint32_t attachment_count, const VkPipelineColorBlendAttachmentState *attachments)
{
    vk_desc->sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    vk_desc->pNext = NULL;
    vk_desc->flags = 0;
    vk_desc->logicOpEnable = d3d12_desc->RenderTarget[0].LogicOpEnable;
    vk_desc->logicOp = vk_logic_op_from_d3d12(d3d12_desc->RenderTarget[0].LogicOp);
    vk_desc->attachmentCount = attachment_count;
    vk_desc->pAttachments = attachments;
    /* Blend constants are dynamic state */
    memset(vk_desc->blendConstants, 0, sizeof(vk_desc->blendConstants));
}

static bool is_dual_source_blending_blend(D3D12_BLEND b)
{
    return b == D3D12_BLEND_SRC1_COLOR || b == D3D12_BLEND_INV_SRC1_COLOR
            || b == D3D12_BLEND_SRC1_ALPHA || b == D3D12_BLEND_INV_SRC1_ALPHA;
}

static bool is_dual_source_blending(const D3D12_RENDER_TARGET_BLEND_DESC *desc)
{
    return desc->BlendEnable
            && (is_dual_source_blending_blend(desc->SrcBlend)
            || is_dual_source_blending_blend(desc->DestBlend)
            || is_dual_source_blending_blend(desc->SrcBlendAlpha)
            || is_dual_source_blending_blend(desc->DestBlendAlpha));
}

static HRESULT compute_input_layout_offsets(const struct d3d12_device *device,
        const D3D12_INPUT_LAYOUT_DESC *input_layout_desc, uint32_t *offsets)
{
    uint32_t input_slot_offsets[D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT] = {0};
    const D3D12_INPUT_ELEMENT_DESC *e;
    const struct vkd3d_format *format;
    unsigned int i;

    if (input_layout_desc->NumElements > D3D12_VS_INPUT_REGISTER_COUNT)
    {
        FIXME("InputLayout.NumElements %u > %u, ignoring extra elements.\n",
                input_layout_desc->NumElements, D3D12_VS_INPUT_REGISTER_COUNT);
    }

    for (i = 0; i < min(input_layout_desc->NumElements, D3D12_VS_INPUT_REGISTER_COUNT); ++i)
    {
        e = &input_layout_desc->pInputElementDescs[i];

        if (e->InputSlot >= ARRAY_SIZE(input_slot_offsets))
        {
            WARN("Invalid input slot %#x.\n", e->InputSlot);
            return E_INVALIDARG;
        }

        if (!(format = vkd3d_get_format(device, e->Format, false)))
        {
            WARN("Invalid input element format %#x.\n", e->Format);
            return E_INVALIDARG;
        }

        if (e->AlignedByteOffset != D3D12_APPEND_ALIGNED_ELEMENT)
            offsets[i] = e->AlignedByteOffset;
        else
            offsets[i] = input_slot_offsets[e->InputSlot];

        input_slot_offsets[e->InputSlot] = align(offsets[i] + format->byte_count, 4);
    }

    return S_OK;
}

static unsigned int vkd3d_get_rt_format_swizzle(const struct vkd3d_format *format)
{
    if (format->dxgi_format == DXGI_FORMAT_A8_UNORM)
        return VKD3D_SWIZZLE(VKD3D_SWIZZLE_W, VKD3D_SWIZZLE_X, VKD3D_SWIZZLE_Y, VKD3D_SWIZZLE_Z);

    return VKD3D_NO_SWIZZLE;
}

STATIC_ASSERT(sizeof(struct vkd3d_shader_transform_feedback_element) == sizeof(D3D12_SO_DECLARATION_ENTRY));

static HRESULT d3d12_graphics_pipeline_state_create_render_pass(
        struct d3d12_graphics_pipeline_state *graphics, struct d3d12_device *device,
        VkFormat dynamic_dsv_format, VkRenderPass *vk_render_pass, VkImageLayout *dsv_layout)
{
    struct vkd3d_render_pass_key key;
    VkFormat dsv_format;
    unsigned int i;

    memcpy(key.vk_formats, graphics->rtv_formats, sizeof(graphics->rtv_formats));
    key.attachment_count = graphics->rt_count;

    if (!(dsv_format = graphics->dsv_format) && (graphics->null_attachment_mask & dsv_attachment_mask(graphics)))
        dsv_format = dynamic_dsv_format;

    if (dsv_format)
    {
        assert(graphics->ds_desc.front.writeMask == graphics->ds_desc.back.writeMask);
        key.depth_enable = graphics->ds_desc.depthTestEnable;
        key.stencil_enable = graphics->ds_desc.stencilTestEnable;
        key.depth_write = key.depth_enable && graphics->ds_desc.depthWriteEnable;
        key.stencil_write = key.stencil_enable && graphics->ds_desc.front.writeMask != 0;
        key.vk_formats[key.attachment_count++] = dsv_format;
    }
    else
    {
        key.depth_enable = false;
        key.stencil_enable = false;
        key.depth_write = false;
        key.stencil_write = false;
    }

    if (key.attachment_count != ARRAY_SIZE(key.vk_formats))
        key.vk_formats[ARRAY_SIZE(key.vk_formats) - 1] = VK_FORMAT_UNDEFINED;
    for (i = key.attachment_count; i < ARRAY_SIZE(key.vk_formats); ++i)
        assert(key.vk_formats[i] == VK_FORMAT_UNDEFINED);

    key.sample_count = graphics->ms_desc.rasterizationSamples;

    if (dsv_layout)
        *dsv_layout = vkd3d_render_pass_get_depth_stencil_layout(&key);

    return vkd3d_render_pass_cache_find(&device->render_pass_cache, device, &key, vk_render_pass);
}

static bool vk_blend_factor_needs_blend_constants(VkBlendFactor blend_factor)
{
    return blend_factor == VK_BLEND_FACTOR_CONSTANT_COLOR ||
            blend_factor == VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR ||
            blend_factor == VK_BLEND_FACTOR_CONSTANT_ALPHA ||
            blend_factor == VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;
}

static bool vk_blend_attachment_needs_blend_constants(const VkPipelineColorBlendAttachmentState *attachment)
{
    return attachment->blendEnable && (
            vk_blend_factor_needs_blend_constants(attachment->srcColorBlendFactor) ||
            vk_blend_factor_needs_blend_constants(attachment->dstColorBlendFactor) ||
            vk_blend_factor_needs_blend_constants(attachment->srcAlphaBlendFactor) ||
            vk_blend_factor_needs_blend_constants(attachment->dstAlphaBlendFactor));
}

static void d3d12_graphics_pipeline_state_init_dynamic_state(struct d3d12_graphics_pipeline_state *graphics,
        bool supports_extended_dynamic_state)
{
    VkPipelineDynamicStateCreateInfo *dynamic_desc_fallback = &graphics->dynamic_desc_fallback;
    VkPipelineDynamicStateCreateInfo *dynamic_desc = &graphics->dynamic_desc;
    unsigned int i, count, count_fallback;

    static const struct
    {
        enum vkd3d_dynamic_state_flag flag;
        VkDynamicState vk_state;
    }
    dynamic_state_list[] =
    {
        { VKD3D_DYNAMIC_STATE_VIEWPORT,             VK_DYNAMIC_STATE_VIEWPORT },
        { VKD3D_DYNAMIC_STATE_SCISSOR,              VK_DYNAMIC_STATE_SCISSOR },
        { VKD3D_DYNAMIC_STATE_VIEWPORT_COUNT,       VK_DYNAMIC_STATE_VIEWPORT_WITH_COUNT_EXT },
        { VKD3D_DYNAMIC_STATE_SCISSOR_COUNT,        VK_DYNAMIC_STATE_SCISSOR_WITH_COUNT_EXT },
        { VKD3D_DYNAMIC_STATE_BLEND_CONSTANTS,      VK_DYNAMIC_STATE_BLEND_CONSTANTS },
        { VKD3D_DYNAMIC_STATE_STENCIL_REFERENCE,    VK_DYNAMIC_STATE_STENCIL_REFERENCE },
        { VKD3D_DYNAMIC_STATE_DEPTH_BOUNDS,         VK_DYNAMIC_STATE_DEPTH_BOUNDS },
        { VKD3D_DYNAMIC_STATE_TOPOLOGY,             VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY_EXT },
        { VKD3D_DYNAMIC_STATE_VERTEX_BUFFER_STRIDE, VK_DYNAMIC_STATE_VERTEX_INPUT_BINDING_STRIDE_EXT },
    };

    graphics->dynamic_state_flags = 0;
    graphics->dynamic_state_flags_fallback = 0;

    /* Enable dynamic states as necessary */
    if (supports_extended_dynamic_state)
    {
        graphics->dynamic_state_flags |= VKD3D_DYNAMIC_STATE_VIEWPORT_COUNT | VKD3D_DYNAMIC_STATE_SCISSOR_COUNT;
        graphics->dynamic_state_flags |= VKD3D_DYNAMIC_STATE_TOPOLOGY;
    }
    else
        graphics->dynamic_state_flags |= VKD3D_DYNAMIC_STATE_VIEWPORT | VKD3D_DYNAMIC_STATE_SCISSOR;

    graphics->dynamic_state_flags_fallback |= VKD3D_DYNAMIC_STATE_VIEWPORT | VKD3D_DYNAMIC_STATE_SCISSOR;

    if (graphics->vertex_buffer_mask)
    {
        graphics->dynamic_state_flags |= supports_extended_dynamic_state ?
                                         VKD3D_DYNAMIC_STATE_VERTEX_BUFFER_STRIDE : VKD3D_DYNAMIC_STATE_VERTEX_BUFFER;
        graphics->dynamic_state_flags_fallback |= VKD3D_DYNAMIC_STATE_VERTEX_BUFFER;
    }

    if (graphics->ds_desc.stencilTestEnable)
    {
        graphics->dynamic_state_flags |= VKD3D_DYNAMIC_STATE_STENCIL_REFERENCE;
        graphics->dynamic_state_flags_fallback |= VKD3D_DYNAMIC_STATE_STENCIL_REFERENCE;
    }

    if (graphics->ds_desc.depthBoundsTestEnable)
    {
        graphics->dynamic_state_flags |= VKD3D_DYNAMIC_STATE_DEPTH_BOUNDS;
        graphics->dynamic_state_flags_fallback |= VKD3D_DYNAMIC_STATE_DEPTH_BOUNDS;
    }

    for (i = 0; i < graphics->rt_count; i++)
    {
        if (vk_blend_attachment_needs_blend_constants(&graphics->blend_attachments[i]))
        {
            graphics->dynamic_state_flags |= VKD3D_DYNAMIC_STATE_BLEND_CONSTANTS;
            graphics->dynamic_state_flags_fallback |= VKD3D_DYNAMIC_STATE_BLEND_CONSTANTS;
        }
    }

    /* Build dynamic state create info */
    for (i = 0, count = 0, count_fallback = 0; i < ARRAY_SIZE(dynamic_state_list); i++)
    {
        if (graphics->dynamic_state_flags & dynamic_state_list[i].flag)
            graphics->dynamic_states[count++] = dynamic_state_list[i].vk_state;
        if (graphics->dynamic_state_flags_fallback & dynamic_state_list[i].flag)
            graphics->dynamic_states_fallback[count_fallback++] = dynamic_state_list[i].vk_state;
    }

    dynamic_desc->sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_desc->pNext = NULL;
    dynamic_desc->flags = 0;
    dynamic_desc->dynamicStateCount = count;
    dynamic_desc->pDynamicStates = graphics->dynamic_states;

    dynamic_desc_fallback->sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_desc_fallback->pNext = NULL;
    dynamic_desc_fallback->flags = 0;
    dynamic_desc_fallback->dynamicStateCount = count_fallback;
    dynamic_desc_fallback->pDynamicStates = graphics->dynamic_states_fallback;
}

static HRESULT d3d12_pipeline_state_init_graphics(struct d3d12_pipeline_state *state,
        struct d3d12_device *device, const struct d3d12_pipeline_state_desc *desc)
{
    const VkPhysicalDeviceFeatures *features = &device->device_info.features2.features;
    bool have_attachment, is_dsv_format_unknown, supports_extended_dynamic_state;
    unsigned int ps_output_swizzle[D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT];
    struct d3d12_graphics_pipeline_state *graphics = &state->graphics;
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    const D3D12_STREAM_OUTPUT_DESC *so_desc = &desc->stream_output;
    VkVertexInputBindingDivisorDescriptionEXT *binding_divisor;
    const struct vkd3d_vulkan_info *vk_info = &device->vk_info;
    const struct vkd3d_shader_compile_arguments *compile_args;
    uint32_t instance_divisors[D3D12_VS_INPUT_REGISTER_COUNT];
    uint32_t aligned_offsets[D3D12_VS_INPUT_REGISTER_COUNT];
    struct vkd3d_shader_compile_arguments ps_compile_args;
    struct vkd3d_shader_parameter ps_shader_parameters[1];
    struct vkd3d_shader_transform_feedback_info xfb_info;
    struct vkd3d_shader_interface_info shader_interface;
    const struct d3d12_root_signature *root_signature;
    struct vkd3d_shader_signature input_signature;
    VkShaderStageFlagBits xfb_stage = 0;
    VkSampleCountFlagBits sample_count;
    const struct vkd3d_format *format;
    unsigned int instance_divisor;
    VkVertexInputRate input_rate;
    unsigned int i, j;
    size_t rt_count;
    uint32_t mask;
    HRESULT hr;
    int ret;

    static const struct
    {
        enum VkShaderStageFlagBits stage;
        ptrdiff_t offset;
    }
    shader_stages[] =
    {
        {VK_SHADER_STAGE_VERTEX_BIT,                  offsetof(struct d3d12_pipeline_state_desc, vs)},
        {VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT,    offsetof(struct d3d12_pipeline_state_desc, hs)},
        {VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT, offsetof(struct d3d12_pipeline_state_desc, ds)},
        {VK_SHADER_STAGE_GEOMETRY_BIT,                offsetof(struct d3d12_pipeline_state_desc, gs)},
        {VK_SHADER_STAGE_FRAGMENT_BIT,                offsetof(struct d3d12_pipeline_state_desc, ps)},
    };

    state->ID3D12PipelineState_iface.lpVtbl = &d3d12_pipeline_state_vtbl;
    state->refcount = 1;

    graphics->stage_count = 0;
    graphics->primitive_topology_type = desc->primitive_topology_type;

    memset(&input_signature, 0, sizeof(input_signature));

    for (i = desc->rtv_formats.NumRenderTargets; i < ARRAY_SIZE(desc->rtv_formats.RTFormats); ++i)
    {
        if (desc->rtv_formats.RTFormats[i] != DXGI_FORMAT_UNKNOWN)
        {
            WARN("Format must be set to DXGI_FORMAT_UNKNOWN for inactive render targets.\n");
            return E_INVALIDARG;
        }
    }

    if (!(root_signature = unsafe_impl_from_ID3D12RootSignature(desc->root_signature)))
    {
        WARN("Root signature is NULL.\n");
        return E_INVALIDARG;
    }

    sample_count = vk_samples_from_dxgi_sample_desc(&desc->sample_desc);
    if (desc->sample_desc.Count != 1 && desc->sample_desc.Quality)
        WARN("Ignoring sample quality %u.\n", desc->sample_desc.Quality);

    rt_count = desc->rtv_formats.NumRenderTargets;
    if (rt_count > ARRAY_SIZE(graphics->blend_attachments))
    {
        FIXME("NumRenderTargets %zu > %zu, ignoring extra formats.\n",
                rt_count, ARRAY_SIZE(graphics->blend_attachments));
        rt_count = ARRAY_SIZE(graphics->blend_attachments);
    }

    graphics->null_attachment_mask = 0;
    for (i = 0; i < rt_count; ++i)
    {
        const D3D12_RENDER_TARGET_BLEND_DESC *rt_desc;

        if (desc->rtv_formats.RTFormats[i] == DXGI_FORMAT_UNKNOWN)
        {
            graphics->null_attachment_mask |= 1u << i;
            ps_output_swizzle[i] = VKD3D_NO_SWIZZLE;
            graphics->rtv_formats[i] = VK_FORMAT_UNDEFINED;
        }
        else if ((format = vkd3d_get_format(device, desc->rtv_formats.RTFormats[i], false)))
        {
            ps_output_swizzle[i] = vkd3d_get_rt_format_swizzle(format);
            graphics->rtv_formats[i] = format->vk_format;
        }
        else
        {
            WARN("Invalid RTV format %#x.\n", desc->rtv_formats.RTFormats[i]);
            hr = E_INVALIDARG;
            goto fail;
        }

        rt_desc = &desc->blend_state.RenderTarget[desc->blend_state.IndependentBlendEnable ? i : 0];
        if (desc->blend_state.IndependentBlendEnable && rt_desc->LogicOpEnable)
        {
            WARN("IndependentBlendEnable must be FALSE when logic operations are enabled.\n");
            hr = E_INVALIDARG;
            goto fail;
        }
        if (rt_desc->BlendEnable && rt_desc->LogicOpEnable)
        {
            WARN("Only one of BlendEnable or LogicOpEnable can be set to TRUE.");
            hr = E_INVALIDARG;
            goto fail;
        }

        blend_attachment_from_d3d12(&graphics->blend_attachments[i], rt_desc);
    }

    for (i = rt_count; i < ARRAY_SIZE(graphics->rtv_formats); ++i)
        graphics->rtv_formats[i] = VK_FORMAT_UNDEFINED;
    graphics->rt_count = rt_count;

    blend_desc_from_d3d12(&graphics->blend_desc, &desc->blend_state,
            graphics->rt_count, graphics->blend_attachments);

    if (graphics->blend_desc.logicOpEnable && !features->logicOp)
    {
        ERR("Logic op not supported by device.\n");
        hr = E_INVALIDARG;
        goto fail;
    }

    ds_desc_from_d3d12(&graphics->ds_desc, &desc->depth_stencil_state);
    if (graphics->ds_desc.depthBoundsTestEnable && !features->depthBounds)
    {
        ERR("Depth bounds test not supported by device.\n");
        hr = E_INVALIDARG;
        goto fail;
    }

    if (desc->dsv_format == DXGI_FORMAT_UNKNOWN
            && graphics->ds_desc.depthTestEnable && !graphics->ds_desc.depthWriteEnable
            && graphics->ds_desc.depthCompareOp == VK_COMPARE_OP_ALWAYS && !graphics->ds_desc.stencilTestEnable)
    {
        TRACE("Disabling depth test.\n");
        graphics->ds_desc.depthTestEnable = VK_FALSE;
    }

    graphics->dsv_format = VK_FORMAT_UNDEFINED;
    if (graphics->ds_desc.depthTestEnable || graphics->ds_desc.stencilTestEnable)
    {
        if (desc->dsv_format == DXGI_FORMAT_UNKNOWN)
        {
            WARN("DSV format is DXGI_FORMAT_UNKNOWN.\n");
            graphics->dsv_format = VK_FORMAT_UNDEFINED;
            graphics->null_attachment_mask |= dsv_attachment_mask(graphics);
        }
        else if ((format = vkd3d_get_format(device, desc->dsv_format, true)))
        {
            if (format->vk_aspect_mask & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT))
                graphics->dsv_format = format->vk_format;
            else
                FIXME("Format %#x is not depth/stencil format.\n", format->dxgi_format);
        }
        else
        {
            WARN("Invalid DSV format %#x.\n", desc->dsv_format);
            hr = E_INVALIDARG;
            goto fail;
        }
    }

    ps_shader_parameters[0].name = VKD3D_SHADER_PARAMETER_NAME_RASTERIZER_SAMPLE_COUNT;
    ps_shader_parameters[0].type = VKD3D_SHADER_PARAMETER_TYPE_IMMEDIATE_CONSTANT;
    ps_shader_parameters[0].data_type = VKD3D_SHADER_PARAMETER_DATA_TYPE_UINT32;
    ps_shader_parameters[0].immediate_constant.u32 = sample_count;

    ps_compile_args.type = VKD3D_SHADER_STRUCTURE_TYPE_COMPILE_ARGUMENTS;
    ps_compile_args.next = NULL;
    ps_compile_args.target = VKD3D_SHADER_TARGET_SPIRV_VULKAN_1_0;
    ps_compile_args.target_extension_count = vk_info->shader_extension_count;
    ps_compile_args.target_extensions = vk_info->shader_extensions;
    ps_compile_args.parameter_count = ARRAY_SIZE(ps_shader_parameters);
    ps_compile_args.parameters = ps_shader_parameters;
    ps_compile_args.dual_source_blending = is_dual_source_blending(&desc->blend_state.RenderTarget[0]);
    ps_compile_args.output_swizzles = ps_output_swizzle;
    ps_compile_args.output_swizzle_count = rt_count;

    if (ps_compile_args.dual_source_blending && rt_count > 1)
    {
        WARN("Only one render target is allowed when dual source blending is used.\n");
        hr = E_INVALIDARG;
        goto fail;
    }
    if (ps_compile_args.dual_source_blending && desc->blend_state.IndependentBlendEnable)
    {
        for (i = 1; i < ARRAY_SIZE(desc->blend_state.RenderTarget); ++i)
        {
            if (desc->blend_state.RenderTarget[i].BlendEnable)
            {
                WARN("Blend enable cannot be set for render target %u when dual source blending is used.\n", i);
                hr = E_INVALIDARG;
                goto fail;
            }
        }
    }

    graphics->xfb_enabled = false;
    if (so_desc->NumEntries)
    {
        if (!(root_signature->d3d12_flags & D3D12_ROOT_SIGNATURE_FLAG_ALLOW_STREAM_OUTPUT))
        {
            WARN("Stream output is used without D3D12_ROOT_SIGNATURE_FLAG_ALLOW_STREAM_OUTPUT.\n");
            hr = E_INVALIDARG;
            goto fail;
        }

        if (!vk_info->EXT_transform_feedback)
        {
            FIXME("Transform feedback is not supported by Vulkan implementation.\n");
            hr = E_NOTIMPL;
            goto fail;
        }

        graphics->xfb_enabled = true;

        xfb_info.type = VKD3D_SHADER_STRUCTURE_TYPE_TRANSFORM_FEEDBACK_INFO;
        xfb_info.next = NULL;

        xfb_info.elements = (const struct vkd3d_shader_transform_feedback_element *)so_desc->pSODeclaration;
        xfb_info.element_count = so_desc->NumEntries;
        xfb_info.buffer_strides = so_desc->pBufferStrides;
        xfb_info.buffer_stride_count = so_desc->NumStrides;

        if (desc->gs.pShaderBytecode)
            xfb_stage = VK_SHADER_STAGE_GEOMETRY_BIT;
        else if (desc->ds.pShaderBytecode)
            xfb_stage = VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
        else
            xfb_stage = VK_SHADER_STAGE_VERTEX_BIT;
    }

    shader_interface.type = VKD3D_SHADER_STRUCTURE_TYPE_SHADER_INTERFACE_INFO;
    shader_interface.next = NULL;
    shader_interface.flags = d3d12_root_signature_get_shader_interface_flags(root_signature);
    shader_interface.descriptor_tables.offset = root_signature->descriptor_table_offset;
    shader_interface.descriptor_tables.count = root_signature->descriptor_table_count;
    shader_interface.bindings = root_signature->bindings;
    shader_interface.binding_count = root_signature->binding_count;
    shader_interface.push_constant_buffers = root_signature->root_constants;
    shader_interface.push_constant_buffer_count = root_signature->root_constant_count;
    shader_interface.push_constant_ubo_binding = &root_signature->push_constant_ubo_binding;

    graphics->patch_vertex_count = 0;

    for (i = 0; i < ARRAY_SIZE(shader_stages); ++i)
    {
        const D3D12_SHADER_BYTECODE *b = (const void *)((uintptr_t)desc + shader_stages[i].offset);
        const struct vkd3d_shader_code dxbc = {b->pShaderBytecode, b->BytecodeLength};

        if (!b->pShaderBytecode)
            continue;

        compile_args = NULL;
        switch (shader_stages[i].stage)
        {
            case VK_SHADER_STAGE_VERTEX_BIT:
                if ((ret = vkd3d_shader_parse_input_signature(&dxbc, &input_signature)) < 0)
                {
                    hr = hresult_from_vkd3d_result(ret);
                    goto fail;
                }
                break;

            case VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT:
                if ((ret = vkd3d_shader_scan_patch_vertex_count(&dxbc, &graphics->patch_vertex_count)) < 0)
                {
                    hr = hresult_from_vkd3d_result(ret);
                    goto fail;
                }
                /* fallthrough */
            case VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT:
                if (desc->primitive_topology_type != D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH)
                {
                    WARN("D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH must be used with tessellation shaders.\n");
                    hr = E_INVALIDARG;
                    goto fail;
                }
                break;

            case VK_SHADER_STAGE_GEOMETRY_BIT:
                break;

            case VK_SHADER_STAGE_FRAGMENT_BIT:
                compile_args = &ps_compile_args;
                break;

            default:
                hr = E_INVALIDARG;
                goto fail;
        }

        shader_interface.next = shader_stages[i].stage == xfb_stage ? &xfb_info : NULL;

        if (FAILED(hr = create_shader_stage(device, &graphics->stages[graphics->stage_count],
                shader_stages[i].stage, b, &shader_interface, compile_args)))
            goto fail;

        ++graphics->stage_count;
    }

    graphics->attribute_count = desc->input_layout.NumElements;
    if (graphics->attribute_count > ARRAY_SIZE(graphics->attributes))
    {
        FIXME("InputLayout.NumElements %zu > %zu, ignoring extra elements.\n",
                graphics->attribute_count, ARRAY_SIZE(graphics->attributes));
        graphics->attribute_count = ARRAY_SIZE(graphics->attributes);
    }

    if (graphics->attribute_count
            && !(root_signature->d3d12_flags & D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT))
    {
        WARN("Input layout is used without D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT.\n");
        hr = E_INVALIDARG;
        goto fail;
    }

    if (FAILED(hr = compute_input_layout_offsets(device, &desc->input_layout, aligned_offsets)))
        goto fail;

    graphics->instance_divisor_count = 0;
    graphics->attribute_binding_count = 0;
    memset(graphics->minimum_vertex_buffer_dynamic_stride, 0, sizeof(graphics->minimum_vertex_buffer_dynamic_stride));

    for (i = 0, j = 0, mask = 0; i < graphics->attribute_count; ++i)
    {
        const D3D12_INPUT_ELEMENT_DESC *e = &desc->input_layout.pInputElementDescs[i];
        const struct vkd3d_shader_signature_element *signature_element;

        if (!(format = vkd3d_get_format(device, e->Format, false)))
        {
            WARN("Invalid input element format %#x.\n", e->Format);
            hr = E_INVALIDARG;
            goto fail;
        }

        if (e->InputSlot >= ARRAY_SIZE(graphics->input_rates)
                || e->InputSlot >= ARRAY_SIZE(instance_divisors))
        {
            WARN("Invalid input slot %#x.\n", e->InputSlot);
            hr = E_INVALIDARG;
            goto fail;
        }

        if (!(signature_element = vkd3d_shader_find_signature_element(&input_signature,
                e->SemanticName, e->SemanticIndex, 0)))
        {
            WARN("Unused input element %u.\n", i);
            continue;
        }

        graphics->attributes[j].location = signature_element->register_index;
        graphics->attributes[j].binding = e->InputSlot;
        graphics->attributes[j].format = format->vk_format;
        if (e->AlignedByteOffset != D3D12_APPEND_ALIGNED_ELEMENT)
            graphics->attributes[j].offset = e->AlignedByteOffset;
        else
            graphics->attributes[j].offset = aligned_offsets[i];

        graphics->minimum_vertex_buffer_dynamic_stride[e->InputSlot] =
                max(graphics->minimum_vertex_buffer_dynamic_stride[e->InputSlot],
                    graphics->attributes[j].offset + format->byte_count);

        ++j;

        switch (e->InputSlotClass)
        {
            case D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA:
                input_rate = VK_VERTEX_INPUT_RATE_VERTEX;
                instance_divisor = 1;
                break;

            case D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA:
                input_rate = VK_VERTEX_INPUT_RATE_INSTANCE;
                instance_divisor = e->InstanceDataStepRate;
                if (instance_divisor > vk_info->max_vertex_attrib_divisor
                        || (!instance_divisor && !vk_info->vertex_attrib_zero_divisor))
                {
                    FIXME("Instance divisor %u not supported by Vulkan implementation.\n", instance_divisor);
                    instance_divisor = 1;
                }
                break;

            default:
                FIXME("Unhandled input slot class %#x on input element %u.\n", e->InputSlotClass, i);
                hr = E_INVALIDARG;
                goto fail;
        }

        if (mask & (1u << e->InputSlot) && (graphics->input_rates[e->InputSlot] != input_rate
                || instance_divisors[e->InputSlot] != instance_divisor))
        {
            FIXME("Input slot rate %#x, instance divisor %u on input element %u conflicts "
                    "with earlier input slot rate %#x, instance divisor %u.\n",
                    input_rate, instance_divisor, e->InputSlot,
                    graphics->input_rates[e->InputSlot], instance_divisors[e->InputSlot]);
            hr = E_INVALIDARG;
            goto fail;
        }

        graphics->input_rates[e->InputSlot] = input_rate;
        instance_divisors[e->InputSlot] = instance_divisor;
        if (instance_divisor != 1 && !(mask & (1u << e->InputSlot)))
        {
            binding_divisor = &graphics->instance_divisors[graphics->instance_divisor_count++];
            binding_divisor->binding = e->InputSlot;
            binding_divisor->divisor = instance_divisor;
        }

        if (!(mask & (1u << e->InputSlot)))
        {
            VkVertexInputBindingDescription *binding = &graphics->attribute_bindings[graphics->attribute_binding_count++];
            binding->binding = e->InputSlot;
            binding->inputRate = input_rate;
            binding->stride = 0; /* To be filled in later. */
        }
        mask |= 1u << e->InputSlot;
    }
    graphics->attribute_count = j;
    graphics->vertex_buffer_mask = mask;
    vkd3d_shader_free_shader_signature(&input_signature);

    switch (desc->strip_cut_value)
    {
        case D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED:
        case D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFF:
        case D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFFFFFF:
            graphics->index_buffer_strip_cut_value = desc->strip_cut_value;
            break;
        default:
            WARN("Invalid index buffer strip cut value %#x.\n", desc->strip_cut_value);
            hr = E_INVALIDARG;
            goto fail;
    }

    is_dsv_format_unknown = graphics->null_attachment_mask & dsv_attachment_mask(graphics);

    rs_desc_from_d3d12(&graphics->rs_desc, &desc->rasterizer_state);
    have_attachment = graphics->rt_count || graphics->dsv_format || is_dsv_format_unknown;
    if ((!have_attachment && !(desc->ps.pShaderBytecode && desc->ps.BytecodeLength))
            || so_desc->RasterizedStream == D3D12_SO_NO_RASTERIZED_STREAM)
        graphics->rs_desc.rasterizerDiscardEnable = VK_TRUE;

    rs_stream_info_from_d3d12(&graphics->rs_stream_info, &graphics->rs_desc, so_desc, vk_info);
    if (vk_info->EXT_depth_clip_enable)
        rs_depth_clip_info_from_d3d12(&graphics->rs_depth_clip_info, &graphics->rs_desc, &desc->rasterizer_state);

    graphics->ms_desc.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    graphics->ms_desc.pNext = NULL;
    graphics->ms_desc.flags = 0;
    graphics->ms_desc.rasterizationSamples = sample_count;
    graphics->ms_desc.sampleShadingEnable = VK_FALSE;
    graphics->ms_desc.minSampleShading = 0.0f;
    graphics->ms_desc.pSampleMask = NULL;
    if (desc->sample_mask != ~0u)
    {
        assert(DIV_ROUND_UP(sample_count, 32) <= ARRAY_SIZE(graphics->sample_mask));
        graphics->sample_mask[0] = desc->sample_mask;
        graphics->sample_mask[1] = 0xffffffffu;
        graphics->ms_desc.pSampleMask = graphics->sample_mask;
    }
    graphics->ms_desc.alphaToCoverageEnable = desc->blend_state.AlphaToCoverageEnable;
    graphics->ms_desc.alphaToOneEnable = VK_FALSE;

    if (desc->view_instancing_desc.ViewInstanceCount)
    {
        ERR("View instancing not supported.\n");
        hr = E_INVALIDARG;
        goto fail;
    }

    supports_extended_dynamic_state = device->device_info.extended_dynamic_state_features.extendedDynamicState &&
            (desc->primitive_topology_type != D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH || graphics->patch_vertex_count != 0) &&
            desc->primitive_topology_type != D3D12_PRIMITIVE_TOPOLOGY_TYPE_UNDEFINED;

    d3d12_graphics_pipeline_state_init_dynamic_state(graphics, supports_extended_dynamic_state);

    graphics->pipeline = VK_NULL_HANDLE;
    graphics->root_signature = root_signature;
    state->device = device;

    if (supports_extended_dynamic_state)
    {
        /* If we have EXT_extended_dynamic_state, we can compile a pipeline right here.
         * There are still some edge cases where we need to fall back to special pipelines, but that should be very rare. */
        graphics->pipeline = d3d12_pipeline_state_create_pipeline_variant(state, NULL, graphics->dsv_format,
                                                                          &graphics->render_pass);
        if (!graphics->pipeline)
            goto fail;
    }
    else if (FAILED(hr = d3d12_graphics_pipeline_state_create_render_pass(graphics,
            device, 0, &graphics->render_pass, &graphics->dsv_layout)))
        goto fail;

    list_init(&graphics->compiled_fallback_pipelines);

    if (FAILED(hr = vkd3d_private_store_init(&state->private_store)))
        goto fail;

    state->vk_bind_point = VK_PIPELINE_BIND_POINT_GRAPHICS;
    d3d12_device_add_ref(state->device);

    return S_OK;

fail:
    for (i = 0; i < graphics->stage_count; ++i)
    {
        VK_CALL(vkDestroyShaderModule(device->vk_device, state->graphics.stages[i].module, NULL));
    }
    vkd3d_shader_free_shader_signature(&input_signature);

    return hr;
}

HRESULT d3d12_pipeline_state_create(struct d3d12_device *device, VkPipelineBindPoint bind_point,
        const struct d3d12_pipeline_state_desc *desc, struct d3d12_pipeline_state **state)
{
    struct d3d12_pipeline_state *object;
    HRESULT hr;

    if (!(object = vkd3d_malloc(sizeof(*object))))
        return E_OUTOFMEMORY;

    switch (bind_point)
    {
        case VK_PIPELINE_BIND_POINT_COMPUTE:
            hr = d3d12_pipeline_state_init_compute(object, device, desc);
            break;

        case VK_PIPELINE_BIND_POINT_GRAPHICS:
            hr = d3d12_pipeline_state_init_graphics(object, device, desc);
            break;

        default:
            ERR("Invalid pipeline type %u.", bind_point);
            hr = E_INVALIDARG;
    }

    if (FAILED(hr))
    {
        vkd3d_free(object);
        return hr;
    }

    TRACE("Created pipeline state %p.\n", object);

    *state = object;
    return S_OK;
}

static bool vkd3d_topology_type_can_restart(D3D12_PRIMITIVE_TOPOLOGY_TYPE type)
{
    switch (type)
    {
        case D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE:
        case D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE:
            return true;

        default:
            return false;
    }
}

static bool vkd3d_topology_can_restart(VkPrimitiveTopology topology)
{
    switch (topology)
    {
    case VK_PRIMITIVE_TOPOLOGY_POINT_LIST:
    case VK_PRIMITIVE_TOPOLOGY_LINE_LIST:
    case VK_PRIMITIVE_TOPOLOGY_PATCH_LIST:
    case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST:
    case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY:
    case VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY:
        return false;

    default:
        return true;
    }
}

static enum VkPrimitiveTopology vk_topology_from_d3d12_topology_type(D3D12_PRIMITIVE_TOPOLOGY_TYPE type, bool restart)
{
    /* Technically shouldn't need to know restart state here, but there is a VU banning use of primitiveRestartEnable
     * with list types. Using a strip type is harmless and is likely to dodge driver bugs. */
    switch (type)
    {
        case D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE:
            return restart ? VK_PRIMITIVE_TOPOLOGY_LINE_STRIP : VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        case D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE:
            return restart ? VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        case D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT:
            return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
        case D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH:
            return VK_PRIMITIVE_TOPOLOGY_PATCH_LIST;
        default:
            ERR("Invalid primitive topology type #%x.\n", (unsigned)type);
            return VK_PRIMITIVE_TOPOLOGY_MAX_ENUM;
    }
}

enum VkPrimitiveTopology vk_topology_from_d3d12_topology(D3D12_PRIMITIVE_TOPOLOGY topology)
{
    switch (topology)
    {
        case D3D_PRIMITIVE_TOPOLOGY_POINTLIST:
            return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
        case D3D_PRIMITIVE_TOPOLOGY_LINELIST:
            return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        case D3D_PRIMITIVE_TOPOLOGY_LINESTRIP:
            return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
        case D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST:
            return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        case D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP:
            return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
        case D3D_PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST:
        case D3D_PRIMITIVE_TOPOLOGY_2_CONTROL_POINT_PATCHLIST:
        case D3D_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST:
        case D3D_PRIMITIVE_TOPOLOGY_4_CONTROL_POINT_PATCHLIST:
        case D3D_PRIMITIVE_TOPOLOGY_5_CONTROL_POINT_PATCHLIST:
        case D3D_PRIMITIVE_TOPOLOGY_6_CONTROL_POINT_PATCHLIST:
        case D3D_PRIMITIVE_TOPOLOGY_7_CONTROL_POINT_PATCHLIST:
        case D3D_PRIMITIVE_TOPOLOGY_8_CONTROL_POINT_PATCHLIST:
        case D3D_PRIMITIVE_TOPOLOGY_9_CONTROL_POINT_PATCHLIST:
        case D3D_PRIMITIVE_TOPOLOGY_10_CONTROL_POINT_PATCHLIST:
        case D3D_PRIMITIVE_TOPOLOGY_11_CONTROL_POINT_PATCHLIST:
        case D3D_PRIMITIVE_TOPOLOGY_12_CONTROL_POINT_PATCHLIST:
        case D3D_PRIMITIVE_TOPOLOGY_13_CONTROL_POINT_PATCHLIST:
        case D3D_PRIMITIVE_TOPOLOGY_14_CONTROL_POINT_PATCHLIST:
        case D3D_PRIMITIVE_TOPOLOGY_15_CONTROL_POINT_PATCHLIST:
        case D3D_PRIMITIVE_TOPOLOGY_16_CONTROL_POINT_PATCHLIST:
        case D3D_PRIMITIVE_TOPOLOGY_17_CONTROL_POINT_PATCHLIST:
        case D3D_PRIMITIVE_TOPOLOGY_18_CONTROL_POINT_PATCHLIST:
        case D3D_PRIMITIVE_TOPOLOGY_19_CONTROL_POINT_PATCHLIST:
        case D3D_PRIMITIVE_TOPOLOGY_20_CONTROL_POINT_PATCHLIST:
        case D3D_PRIMITIVE_TOPOLOGY_21_CONTROL_POINT_PATCHLIST:
        case D3D_PRIMITIVE_TOPOLOGY_22_CONTROL_POINT_PATCHLIST:
        case D3D_PRIMITIVE_TOPOLOGY_23_CONTROL_POINT_PATCHLIST:
        case D3D_PRIMITIVE_TOPOLOGY_24_CONTROL_POINT_PATCHLIST:
        case D3D_PRIMITIVE_TOPOLOGY_25_CONTROL_POINT_PATCHLIST:
        case D3D_PRIMITIVE_TOPOLOGY_26_CONTROL_POINT_PATCHLIST:
        case D3D_PRIMITIVE_TOPOLOGY_27_CONTROL_POINT_PATCHLIST:
        case D3D_PRIMITIVE_TOPOLOGY_28_CONTROL_POINT_PATCHLIST:
        case D3D_PRIMITIVE_TOPOLOGY_29_CONTROL_POINT_PATCHLIST:
        case D3D_PRIMITIVE_TOPOLOGY_30_CONTROL_POINT_PATCHLIST:
        case D3D_PRIMITIVE_TOPOLOGY_31_CONTROL_POINT_PATCHLIST:
        case D3D_PRIMITIVE_TOPOLOGY_32_CONTROL_POINT_PATCHLIST:
            return VK_PRIMITIVE_TOPOLOGY_PATCH_LIST;
        default:
            FIXME("Unhandled primitive topology %#x.\n", topology);
            return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    }
}

static VkPipeline d3d12_pipeline_state_find_compiled_pipeline(const struct d3d12_pipeline_state *state,
        const struct vkd3d_pipeline_key *key, VkRenderPass *vk_render_pass)
{
    const struct d3d12_graphics_pipeline_state *graphics = &state->graphics;
    struct d3d12_device *device = state->device;
    VkPipeline vk_pipeline = VK_NULL_HANDLE;
    struct vkd3d_compiled_pipeline *current;
    int rc;

    *vk_render_pass = VK_NULL_HANDLE;

    if (!(rc = pthread_mutex_lock(&device->mutex)))
    {
        LIST_FOR_EACH_ENTRY(current, &graphics->compiled_fallback_pipelines, struct vkd3d_compiled_pipeline, entry)
        {
            if (!memcmp(&current->key, key, sizeof(*key)))
            {
                vk_pipeline = current->vk_pipeline;
                *vk_render_pass = current->vk_render_pass;
                break;
            }
        }
        pthread_mutex_unlock(&device->mutex);
    }
    else
    {
        ERR("Failed to lock mutex, error %d.\n", rc);
    }

    return vk_pipeline;
}

static bool d3d12_pipeline_state_put_pipeline_to_cache(struct d3d12_pipeline_state *state,
        const struct vkd3d_pipeline_key *key, VkPipeline vk_pipeline, VkRenderPass vk_render_pass)
{
    struct d3d12_graphics_pipeline_state *graphics = &state->graphics;
    struct vkd3d_compiled_pipeline *compiled_pipeline, *current;
    struct d3d12_device *device = state->device;
    int rc;

    if (!(compiled_pipeline = vkd3d_malloc(sizeof(*compiled_pipeline))))
        return false;

    compiled_pipeline->key = *key;
    compiled_pipeline->vk_pipeline = vk_pipeline;
    compiled_pipeline->vk_render_pass = vk_render_pass;

    if ((rc = pthread_mutex_lock(&device->mutex)))
    {
        ERR("Failed to lock mutex, error %d.\n", rc);
        vkd3d_free(compiled_pipeline);
        return false;
    }

    LIST_FOR_EACH_ENTRY(current, &graphics->compiled_fallback_pipelines, struct vkd3d_compiled_pipeline, entry)
    {
        if (!memcmp(&current->key, key, sizeof(*key)))
        {
            vkd3d_free(compiled_pipeline);
            compiled_pipeline = NULL;
            break;
        }
    }

    if (compiled_pipeline)
        list_add_tail(&graphics->compiled_fallback_pipelines, &compiled_pipeline->entry);

    pthread_mutex_unlock(&device->mutex);
    return compiled_pipeline;
}

VkPipeline d3d12_pipeline_state_create_pipeline_variant(struct d3d12_pipeline_state *state,
        const struct vkd3d_dynamic_state *dyn_state, VkFormat dsv_format, VkRenderPass *vk_render_pass)
{
    VkVertexInputBindingDescription bindings[D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
    const struct vkd3d_vk_device_procs *vk_procs = &state->device->vk_procs;
    struct d3d12_graphics_pipeline_state *graphics = &state->graphics;
    VkPipelineVertexInputDivisorStateCreateInfoEXT input_divisor_info;
    VkPipelineTessellationStateCreateInfo tessellation_info;
    VkPipelineVertexInputStateCreateInfo input_desc;
    VkPipelineInputAssemblyStateCreateInfo ia_desc;
    struct d3d12_device *device = state->device;
    VkGraphicsPipelineCreateInfo pipeline_desc;
    VkPipelineViewportStateCreateInfo vp_desc;
    VkPipeline vk_pipeline;
    unsigned int i;
    VkResult vr;
    HRESULT hr;

    memcpy(bindings, graphics->attribute_bindings, graphics->attribute_binding_count * sizeof(*bindings));

    if (dyn_state)
    {
        /* If not using extended dynamic state, set static vertex stride. */
        for (i = 0; i < graphics->attribute_binding_count; i++)
            bindings[i].stride = dyn_state->vertex_strides[bindings[i].binding];
    }

    input_desc.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    input_desc.pNext = NULL;
    input_desc.flags = 0;
    input_desc.vertexBindingDescriptionCount = graphics->attribute_binding_count;
    input_desc.pVertexBindingDescriptions = bindings;
    input_desc.vertexAttributeDescriptionCount = graphics->attribute_count;
    input_desc.pVertexAttributeDescriptions = graphics->attributes;

    if (graphics->instance_divisor_count)
    {
        input_desc.pNext = &input_divisor_info;
        input_divisor_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_DIVISOR_STATE_CREATE_INFO_EXT;
        input_divisor_info.pNext = NULL;
        input_divisor_info.vertexBindingDivisorCount = graphics->instance_divisor_count;
        input_divisor_info.pVertexBindingDivisors = graphics->instance_divisors;
    }

    ia_desc.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia_desc.pNext = NULL;
    ia_desc.flags = 0;
    ia_desc.topology = dyn_state ?
            vk_topology_from_d3d12_topology(dyn_state->primitive_topology) :
            vk_topology_from_d3d12_topology_type(graphics->primitive_topology_type, !!graphics->index_buffer_strip_cut_value);
    ia_desc.primitiveRestartEnable = graphics->index_buffer_strip_cut_value &&
                                     (dyn_state ?
                                      vkd3d_topology_can_restart(ia_desc.topology) :
                                      vkd3d_topology_type_can_restart(graphics->primitive_topology_type));

    tessellation_info.sType = VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO;
    tessellation_info.pNext = NULL;
    tessellation_info.flags = 0;
    tessellation_info.patchControlPoints = dyn_state ?
            max(dyn_state->primitive_topology - D3D_PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST + 1, 1) :
            graphics->patch_vertex_count;

    vp_desc.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp_desc.pNext = NULL;
    vp_desc.flags = 0;
    vp_desc.viewportCount = dyn_state ? max(dyn_state->viewport_count, 1) : 0;
    vp_desc.pViewports = NULL;
    vp_desc.scissorCount = dyn_state ? max(dyn_state->viewport_count, 1) : 0;
    vp_desc.pScissors = NULL;

    pipeline_desc.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_desc.pNext = NULL;
    pipeline_desc.flags = 0;
    pipeline_desc.stageCount = graphics->stage_count;
    pipeline_desc.pStages = graphics->stages;
    pipeline_desc.pVertexInputState = &input_desc;
    pipeline_desc.pInputAssemblyState = &ia_desc;
    pipeline_desc.pTessellationState = &tessellation_info;
    pipeline_desc.pViewportState = &vp_desc;
    pipeline_desc.pRasterizationState = &graphics->rs_desc;
    pipeline_desc.pMultisampleState = &graphics->ms_desc;
    pipeline_desc.pDepthStencilState = &graphics->ds_desc;
    pipeline_desc.pColorBlendState = &graphics->blend_desc;
    pipeline_desc.pDynamicState = dyn_state ? &graphics->dynamic_desc_fallback : &graphics->dynamic_desc;
    pipeline_desc.layout = graphics->root_signature->vk_pipeline_layout;
    pipeline_desc.subpass = 0;
    pipeline_desc.basePipelineHandle = VK_NULL_HANDLE;
    pipeline_desc.basePipelineIndex = -1;

    pipeline_desc.renderPass = graphics->render_pass;

    /* A workaround for SottR, which creates pipelines with DSV_UNKNOWN, but still insists on using a depth buffer.
     * If we notice that the base pipeline's DSV format does not match the dynamic DSV format, we fall-back to create a new render pass. */
    if (graphics->dsv_format != dsv_format && (graphics->null_attachment_mask & dsv_attachment_mask(graphics)))
        TRACE("Compiling %p with fallback DSV format %#x.\n", state, dsv_format);

    if (FAILED(hr = d3d12_graphics_pipeline_state_create_render_pass(graphics, device, dsv_format,
            &pipeline_desc.renderPass, &graphics->dsv_layout)))
        return VK_NULL_HANDLE;

    *vk_render_pass = pipeline_desc.renderPass;

    if ((vr = VK_CALL(vkCreateGraphicsPipelines(device->vk_device, device->vk_pipeline_cache,
            1, &pipeline_desc, NULL, &vk_pipeline))) < 0)
    {
        WARN("Failed to create Vulkan graphics pipeline, vr %d.\n", vr);
        return VK_NULL_HANDLE;
    }

    return vk_pipeline;
}

static bool d3d12_pipeline_state_can_use_dynamic_stride(struct d3d12_pipeline_state *state,
        const struct vkd3d_dynamic_state *dyn_state)
{
    struct d3d12_graphics_pipeline_state *graphics = &state->graphics;
    uint32_t vertex_mask = graphics->vertex_buffer_mask;
    unsigned int slot;

    while (vertex_mask)
    {
        slot = vkd3d_bitmask_iter32(&vertex_mask);
        /* The vertex buffer stride must be larger than any attribute offset + format size which accesses a buffer binding.
         * This is somewhat awkward, since D3D12 does not have this restriction, although the validation layers do warn about this.
         * There might also be similar fallback paths on certain native drivers, who knows ... */
        if (dyn_state->vertex_strides[slot] < graphics->minimum_vertex_buffer_dynamic_stride[slot])
        {
            TRACE("Stride for slot %u is %u bytes, but need at least %u.\n", slot,
                  (unsigned int)dyn_state->vertex_strides[slot],
                  graphics->minimum_vertex_buffer_dynamic_stride[slot]);
            return false;
        }
    }

    return true;
}

VkPipeline d3d12_pipeline_state_get_pipeline(struct d3d12_pipeline_state *state,
        const struct vkd3d_dynamic_state *dyn_state, VkFormat dsv_format, VkRenderPass *vk_render_pass)
{
    struct d3d12_graphics_pipeline_state *graphics = &state->graphics;

    if (!graphics->pipeline)
        return VK_NULL_HANDLE;

    /* Unknown DSV format workaround. */
    if (dsv_format != graphics->dsv_format)
    {
        TRACE("DSV format mismatch, expected %u, got %u, buggy application!\n",
              graphics->dsv_format, dsv_format);
        return VK_NULL_HANDLE;
    }

    if (!d3d12_pipeline_state_can_use_dynamic_stride(state, dyn_state))
    {
        TRACE("Cannot use dynamic stride, falling back ...\n");
        return VK_NULL_HANDLE;
    }

    /* It should be illegal to use different patch size for topology compared to pipeline, but be safe here. */
    if (dyn_state->vk_primitive_topology == VK_PRIMITIVE_TOPOLOGY_PATCH_LIST &&
        (dyn_state->primitive_topology - D3D_PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST + 1) != graphics->patch_vertex_count)
    {
        if (graphics->patch_vertex_count)
        {
            TRACE("Mismatch in tessellation control points, expected %u, but got %u.\n",
                  graphics->patch_vertex_count,
                  dyn_state->primitive_topology - D3D_PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST + 1);
        }
        return VK_NULL_HANDLE;
    }

    *vk_render_pass = state->graphics.render_pass;
    return state->graphics.pipeline;
}

VkPipeline d3d12_pipeline_state_get_or_create_pipeline(struct d3d12_pipeline_state *state,
        const struct vkd3d_dynamic_state *dyn_state, VkFormat dsv_format, VkRenderPass *vk_render_pass)
{
    const struct vkd3d_vk_device_procs *vk_procs = &state->device->vk_procs;
    struct d3d12_graphics_pipeline_state *graphics = &state->graphics;
    struct d3d12_device *device = state->device;
    struct vkd3d_pipeline_key pipeline_key;
    size_t binding_count = 0;
    VkPipeline vk_pipeline;
    unsigned int i;
    uint32_t mask;

    assert(d3d12_pipeline_state_is_graphics(state));

    memset(&pipeline_key, 0, sizeof(pipeline_key));
    pipeline_key.topology = dyn_state->primitive_topology;
    pipeline_key.viewport_count = max(dyn_state->viewport_count, 1);

    for (i = 0, mask = 0; i < graphics->attribute_count; ++i)
    {
        uint32_t binding = graphics->attributes[i].binding;
        if (mask & (1u << binding))
            continue;

        mask |= 1u << binding;
        pipeline_key.strides[binding_count] = dyn_state->vertex_strides[binding];
        ++binding_count;
    }

    pipeline_key.dsv_format = dsv_format;

    if ((vk_pipeline = d3d12_pipeline_state_find_compiled_pipeline(state, &pipeline_key, vk_render_pass)))
        return vk_pipeline;

    if (!device->device_info.extended_dynamic_state_features.extendedDynamicState)
        FIXME("Extended dynamic state is supported, but compiling a fallback pipeline late!\n");

    vk_pipeline = d3d12_pipeline_state_create_pipeline_variant(state, dyn_state, dsv_format, vk_render_pass);
    if (!vk_pipeline)
    {
        ERR("Failed to create pipeline.\n");
        return VK_NULL_HANDLE;
    }

    if (d3d12_pipeline_state_put_pipeline_to_cache(state, &pipeline_key, vk_pipeline, *vk_render_pass))
        return vk_pipeline;
    /* Other thread compiled the pipeline before us. */
    VK_CALL(vkDestroyPipeline(device->vk_device, vk_pipeline, NULL));
    vk_pipeline = d3d12_pipeline_state_find_compiled_pipeline(state, &pipeline_key, vk_render_pass);
    if (!vk_pipeline)
        ERR("Could not get the pipeline compiled by other thread from the cache.\n");
    return vk_pipeline;
}

static D3D12_DESCRIPTOR_HEAP_TYPE d3d12_descriptor_heap_type_from_range_type(D3D12_DESCRIPTOR_RANGE_TYPE range_type)
{
    switch (range_type)
    {
        case D3D12_DESCRIPTOR_RANGE_TYPE_CBV:
        case D3D12_DESCRIPTOR_RANGE_TYPE_SRV:
        case D3D12_DESCRIPTOR_RANGE_TYPE_UAV:
            return D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;

        case D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER:
            return D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;

        default:
            ERR("Invalid descriptor range type %d.\n", range_type);
            return D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    }
}

static uint32_t d3d12_max_descriptor_count_from_range_type(D3D12_DESCRIPTOR_RANGE_TYPE range_type)
{
    switch (range_type)
    {
        case D3D12_DESCRIPTOR_RANGE_TYPE_CBV:
        case D3D12_DESCRIPTOR_RANGE_TYPE_SRV:
        case D3D12_DESCRIPTOR_RANGE_TYPE_UAV:
            return 1000000;

        case D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER:
            return 2048;

        default:
            ERR("Invalid descriptor range type %d.\n", range_type);
            return 0;
    }
}

static HRESULT vkd3d_bindless_state_add_binding(struct vkd3d_bindless_state *bindless_state,
        struct d3d12_device *device, D3D12_DESCRIPTOR_RANGE_TYPE range_type,
        enum vkd3d_shader_binding_flag binding_flag)
{
    struct vkd3d_bindless_set_info *set_info = &bindless_state->set_info[bindless_state->set_count++];
    VkDescriptorSetLayoutBindingFlagsCreateInfoEXT vk_binding_flags_info;
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    VkDescriptorSetLayoutCreateInfo vk_set_layout_info;
    VkDescriptorSetLayoutBinding vk_binding_info;
    VkDescriptorBindingFlagsEXT vk_binding_flags;
    VkResult vr;

    set_info->vk_descriptor_type = vk_descriptor_type_from_d3d12_range_type(bindless_state,
            range_type, binding_flag & VKD3D_SHADER_BINDING_FLAG_BUFFER);
    set_info->heap_type = d3d12_descriptor_heap_type_from_range_type(range_type);
    set_info->range_type = range_type;
    set_info->binding_flag = binding_flag;

    vk_binding_flags = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT_EXT |
            VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT_EXT |
            VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT_EXT |
            VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT_EXT;

    vk_binding_flags_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO_EXT;
    vk_binding_flags_info.pNext = NULL;
    vk_binding_flags_info.bindingCount = 1;
    vk_binding_flags_info.pBindingFlags = &vk_binding_flags;

    vk_binding_info.binding = 0;
    vk_binding_info.descriptorType = set_info->vk_descriptor_type;
    vk_binding_info.descriptorCount = d3d12_max_descriptor_count_from_range_type(range_type);
    vk_binding_info.stageFlags = VK_SHADER_STAGE_ALL;
    vk_binding_info.pImmutableSamplers = NULL;

    vk_set_layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    vk_set_layout_info.pNext = &vk_binding_flags_info;
    vk_set_layout_info.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT_EXT;
    vk_set_layout_info.bindingCount = 1;
    vk_set_layout_info.pBindings = &vk_binding_info;

    if ((vr = VK_CALL(vkCreateDescriptorSetLayout(device->vk_device,
            &vk_set_layout_info, NULL, &set_info->vk_set_layout))) < 0)
        ERR("Failed to create descriptor set layout, vr %d.\n", vr);

    return hresult_from_vk_result(vr);
}

static uint32_t vkd3d_bindless_state_get_bindless_flags(struct d3d12_device *device)
{
    const struct vkd3d_physical_device_info *device_info = &device->device_info;
    const struct vkd3d_vulkan_info *vk_info = &device->vk_info;
    uint32_t flags = 0;

    if (!vk_info->EXT_descriptor_indexing ||
            !device_info->descriptor_indexing_features.runtimeDescriptorArray ||
            !device_info->descriptor_indexing_features.descriptorBindingPartiallyBound ||
            !device_info->descriptor_indexing_features.descriptorBindingUpdateUnusedWhilePending ||
            !device_info->descriptor_indexing_features.descriptorBindingVariableDescriptorCount)
        return 0;

    if (device_info->descriptor_indexing_properties.maxPerStageDescriptorUpdateAfterBindSampledImages >= 1000000 &&
            device_info->descriptor_indexing_features.descriptorBindingSampledImageUpdateAfterBind &&
            device_info->descriptor_indexing_features.descriptorBindingUniformTexelBufferUpdateAfterBind &&
            device_info->descriptor_indexing_features.shaderSampledImageArrayNonUniformIndexing &&
            device_info->descriptor_indexing_features.shaderUniformTexelBufferArrayNonUniformIndexing)
        flags |= VKD3D_BINDLESS_SAMPLER | VKD3D_BINDLESS_SRV;

    if (device_info->descriptor_indexing_properties.maxPerStageDescriptorUpdateAfterBindStorageImages >= 1000000 &&
            device_info->descriptor_indexing_features.descriptorBindingStorageImageUpdateAfterBind &&
            device_info->descriptor_indexing_features.descriptorBindingStorageTexelBufferUpdateAfterBind &&
            device_info->descriptor_indexing_features.shaderStorageImageArrayNonUniformIndexing &&
            device_info->descriptor_indexing_features.shaderStorageTexelBufferArrayNonUniformIndexing)
        flags |= VKD3D_BINDLESS_UAV;

#if 0
    /* NVIDIA drivers currently (as of 2020-03-25) seem to have some rather interesting issues with bindless UBO where bindless SSBO
     * appears to work just fine. AMD does not care about UBO vs SSBO, so just use bindless SSBO until the issues are resolved. */
    if (device_info->descriptor_indexing_properties.maxPerStageDescriptorUpdateAfterBindUniformBuffers >= 1000000 &&
            device_info->descriptor_indexing_features.descriptorBindingUniformBufferUpdateAfterBind &&
            device_info->descriptor_indexing_features.shaderUniformBufferArrayNonUniformIndexing)
        flags |= VKD3D_BINDLESS_CBV;
    else
#endif
    if (device_info->descriptor_indexing_properties.maxPerStageDescriptorUpdateAfterBindStorageBuffers >= 1000000 &&
            device_info->descriptor_indexing_features.descriptorBindingStorageBufferUpdateAfterBind &&
            device_info->descriptor_indexing_features.shaderStorageBufferArrayNonUniformIndexing)
        flags |= VKD3D_BINDLESS_CBV | VKD3D_BINDLESS_CBV_AS_SSBO;

    if (device_info->buffer_device_address_features.bufferDeviceAddress && (flags & VKD3D_BINDLESS_UAV))
        flags |= VKD3D_BINDLESS_UAV_COUNTER;

    return flags;
}

HRESULT vkd3d_bindless_state_init(struct vkd3d_bindless_state *bindless_state,
        struct d3d12_device *device)
{
    HRESULT hr;

    memset(bindless_state, 0, sizeof(*bindless_state));
    bindless_state->flags = vkd3d_bindless_state_get_bindless_flags(device);

    if (!bindless_state->flags)
        return S_OK;

    if (bindless_state->flags & VKD3D_BINDLESS_SAMPLER)
    {
        if (FAILED(hr = vkd3d_bindless_state_add_binding(bindless_state, device,
                D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, VKD3D_SHADER_BINDING_FLAG_IMAGE)))
            goto fail;
    }

    if (bindless_state->flags & VKD3D_BINDLESS_CBV)
    {
        if (FAILED(hr = vkd3d_bindless_state_add_binding(bindless_state, device,
                D3D12_DESCRIPTOR_RANGE_TYPE_CBV, VKD3D_SHADER_BINDING_FLAG_BUFFER)))
            goto fail;
    }

    if (bindless_state->flags & VKD3D_BINDLESS_SRV)
    {
        if (FAILED(hr = vkd3d_bindless_state_add_binding(bindless_state, device,
                D3D12_DESCRIPTOR_RANGE_TYPE_SRV, VKD3D_SHADER_BINDING_FLAG_BUFFER)) ||
            FAILED(hr = vkd3d_bindless_state_add_binding(bindless_state, device,
                D3D12_DESCRIPTOR_RANGE_TYPE_SRV, VKD3D_SHADER_BINDING_FLAG_IMAGE)))
            goto fail;
    }

    if (bindless_state->flags & VKD3D_BINDLESS_UAV)
    {
        if (FAILED(hr = vkd3d_bindless_state_add_binding(bindless_state, device,
                D3D12_DESCRIPTOR_RANGE_TYPE_UAV, VKD3D_SHADER_BINDING_FLAG_BUFFER)) ||
            FAILED(hr = vkd3d_bindless_state_add_binding(bindless_state, device,
                D3D12_DESCRIPTOR_RANGE_TYPE_UAV, VKD3D_SHADER_BINDING_FLAG_IMAGE)))
            goto fail;
    }

    return S_OK;

fail:
    vkd3d_bindless_state_cleanup(bindless_state, device);
    return hr;
}

void vkd3d_bindless_state_cleanup(struct vkd3d_bindless_state *bindless_state,
        struct d3d12_device *device)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    unsigned int i;

    for (i = 0; i < bindless_state->set_count; i++)
        VK_CALL(vkDestroyDescriptorSetLayout(device->vk_device, bindless_state->set_info[i].vk_set_layout, NULL));
}

bool vkd3d_bindless_state_find_binding(const struct vkd3d_bindless_state *bindless_state,
        D3D12_DESCRIPTOR_RANGE_TYPE range_type, enum vkd3d_shader_binding_flag binding_flag,
        struct vkd3d_shader_descriptor_binding *binding)
{
    unsigned int i;

    for (i = 0; i < bindless_state->set_count; i++)
    {
        const struct vkd3d_bindless_set_info *set_info = &bindless_state->set_info[i];

        if (set_info->range_type == range_type && set_info->binding_flag == binding_flag)
        {
            binding->set = i;
            binding->binding = 0;
            return true;
        }
    }

    return false;
}

/* ID3D12PipelineLibrary */
static inline struct d3d12_pipeline_library *impl_from_ID3D12PipelineLibrary(d3d12_pipeline_library_iface *iface)
{
    return CONTAINING_RECORD(iface, struct d3d12_pipeline_library, ID3D12PipelineLibrary_iface);
}

static HRESULT STDMETHODCALLTYPE d3d12_pipeline_library_QueryInterface(d3d12_pipeline_library_iface *iface,
        REFIID riid, void **object)
{
    TRACE("iface %p, riid %s, object %p.\n", iface, debugstr_guid(riid), object);

    if (IsEqualGUID(riid, &IID_ID3D12PipelineLibrary)
            || IsEqualGUID(riid, &IID_ID3D12PipelineLibrary1)
            || IsEqualGUID(riid, &IID_ID3D12DeviceChild)
            || IsEqualGUID(riid, &IID_ID3D12Object)
            || IsEqualGUID(riid, &IID_IUnknown))
    {
        ID3D12PipelineLibrary_AddRef(iface);
        *object = iface;
        return S_OK;
    }

    WARN("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(riid));

    *object = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE d3d12_pipeline_library_AddRef(d3d12_pipeline_library_iface *iface)
{
    struct d3d12_pipeline_library *pipeline_library = impl_from_ID3D12PipelineLibrary(iface);
    ULONG refcount = InterlockedIncrement(&pipeline_library->refcount);

    TRACE("%p increasing refcount to %u.\n", pipeline_library, refcount);

    return refcount;
}

static ULONG STDMETHODCALLTYPE d3d12_pipeline_library_Release(d3d12_pipeline_library_iface *iface)
{
    struct d3d12_pipeline_library *pipeline_library = impl_from_ID3D12PipelineLibrary(iface);
    ULONG refcount = InterlockedDecrement(&pipeline_library->refcount);

    TRACE("%p decreasing refcount to %u.\n", pipeline_library, refcount);

    if (!refcount)
    {
        struct d3d12_device *device = pipeline_library->device;
        vkd3d_private_store_destroy(&pipeline_library->private_store);
        vkd3d_free(pipeline_library);
        d3d12_device_release(device);
    }

    return refcount;
}

static HRESULT STDMETHODCALLTYPE d3d12_pipeline_library_GetPrivateData(d3d12_pipeline_library_iface *iface,
        REFGUID guid, UINT *data_size, void *data)
{
    struct d3d12_pipeline_library *pipeline_library = impl_from_ID3D12PipelineLibrary(iface);

    TRACE("iface %p, guid %s, data_size %p, data %p.\n", iface, debugstr_guid(guid), data_size, data);

    return vkd3d_get_private_data(&pipeline_library->private_store, guid, data_size, data);
}

static HRESULT STDMETHODCALLTYPE d3d12_pipeline_library_SetPrivateData(d3d12_pipeline_library_iface *iface,
        REFGUID guid, UINT data_size, const void *data)
{
    struct d3d12_pipeline_library *pipeline_library = impl_from_ID3D12PipelineLibrary(iface);

    TRACE("iface %p, guid %s, data_size %u, data %p.\n", iface, debugstr_guid(guid), data_size, data);

    return vkd3d_set_private_data(&pipeline_library->private_store, guid, data_size, data);
}

static HRESULT STDMETHODCALLTYPE d3d12_pipeline_library_SetPrivateDataInterface(d3d12_pipeline_library_iface *iface,
        REFGUID guid, const IUnknown *data)
{
    struct d3d12_pipeline_library *pipeline_library = impl_from_ID3D12PipelineLibrary(iface);

    TRACE("iface %p, guid %s, data %p.\n", iface, debugstr_guid(guid), data);

    return vkd3d_set_private_data_interface(&pipeline_library->private_store, guid, data);
}

static HRESULT STDMETHODCALLTYPE d3d12_pipeline_library_SetName(d3d12_pipeline_library_iface *iface, const WCHAR *name)
{
    struct d3d12_pipeline_library *pipeline_library = impl_from_ID3D12PipelineLibrary(iface);

    TRACE("iface %p, name %s.\n", iface, debugstr_w(name, pipeline_library->device->wchar_size));

    return name ? S_OK : E_INVALIDARG;
}

static HRESULT STDMETHODCALLTYPE d3d12_pipeline_library_GetDevice(d3d12_pipeline_library_iface *iface,
        REFIID iid, void **device)
{
    struct d3d12_pipeline_library *pipeline_library = impl_from_ID3D12PipelineLibrary(iface);

    TRACE("iface %p, iid %s, device %p.\n", iface, debugstr_guid(iid), device);

    return d3d12_device_query_interface(pipeline_library->device, iid, device);
}

static HRESULT STDMETHODCALLTYPE d3d12_pipeline_library_StorePipeline(d3d12_pipeline_library_iface *iface,
        LPCWSTR name, ID3D12PipelineState *pipeline)
{
    struct d3d12_pipeline_library *pipeline_library = impl_from_ID3D12PipelineLibrary(iface);

    FIXME("iface %p, name %s, pipeline %p stub!\n", iface, debugstr_w(name, pipeline_library->device->wchar_size), pipeline);

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d12_pipeline_library_LoadGraphicsPipeline(d3d12_pipeline_library_iface *iface,
        LPCWSTR name, const D3D12_GRAPHICS_PIPELINE_STATE_DESC *desc, REFIID iid, void **pipeline_state)
{
    struct d3d12_pipeline_library *pipeline_library = impl_from_ID3D12PipelineLibrary(iface);

    FIXME("iface %p, name %s, desc %p, iid %s, pipeline_state %p stub!\n", iface,
            debugstr_w(name, pipeline_library->device->wchar_size),
            desc, debugstr_guid(iid), pipeline_state);

    return E_INVALIDARG;
}

static HRESULT STDMETHODCALLTYPE d3d12_pipeline_library_LoadComputePipeline(d3d12_pipeline_library_iface *iface,
        LPCWSTR name, const D3D12_COMPUTE_PIPELINE_STATE_DESC *desc, REFIID iid, void **pipeline_state)
{
    struct d3d12_pipeline_library *pipeline_library = impl_from_ID3D12PipelineLibrary(iface);

    FIXME("iface %p, name %s, desc %p, iid %s, pipeline_state %p stub!\n", iface,
            debugstr_w(name, pipeline_library->device->wchar_size),
            desc, debugstr_guid(iid), pipeline_state);

    return E_INVALIDARG;
}

static SIZE_T STDMETHODCALLTYPE d3d12_pipeline_library_GetSerializedSize(d3d12_pipeline_library_iface *iface)
{
    FIXME("iface %p stub!\n", iface);

    return 0;
}

static HRESULT STDMETHODCALLTYPE d3d12_pipeline_library_Serialize(d3d12_pipeline_library_iface *iface,
        void *data, SIZE_T data_size)
{
    FIXME("iface %p, data %p, data_size %lu stub!\n", iface, data, data_size);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_pipeline_library_LoadPipeline(d3d12_pipeline_library_iface *iface,
        LPCWSTR name, const D3D12_PIPELINE_STATE_STREAM_DESC *desc, REFIID iid, void **pipeline_state)
{
    struct d3d12_pipeline_library *pipeline_library = impl_from_ID3D12PipelineLibrary(iface);

    FIXME("iface %p, name %s, desc %p, iid %s, pipeline_state %p stub!\n", iface,
            debugstr_w(name, pipeline_library->device->wchar_size),
            desc, debugstr_guid(iid), pipeline_state);

    return E_INVALIDARG;
}

static CONST_VTBL struct ID3D12PipelineLibrary1Vtbl d3d12_pipeline_library_vtbl =
{
    /* IUnknown methods */
    d3d12_pipeline_library_QueryInterface,
    d3d12_pipeline_library_AddRef,
    d3d12_pipeline_library_Release,
    /* ID3D12Object methods */
    d3d12_pipeline_library_GetPrivateData,
    d3d12_pipeline_library_SetPrivateData,
    d3d12_pipeline_library_SetPrivateDataInterface,
    d3d12_pipeline_library_SetName,
    /* ID3D12DeviceChild methods */
    d3d12_pipeline_library_GetDevice,
    /* ID3D12PipelineLibrary methods */
    d3d12_pipeline_library_StorePipeline,
    d3d12_pipeline_library_LoadGraphicsPipeline,
    d3d12_pipeline_library_LoadComputePipeline,
    d3d12_pipeline_library_GetSerializedSize,
    d3d12_pipeline_library_Serialize,
    /* ID3D12PipelineLibrary1 methods */
    d3d12_pipeline_library_LoadPipeline,
};

static HRESULT d3d12_pipeline_library_init(struct d3d12_pipeline_library *pipeline_library,
        struct d3d12_device *device, const void *blob, size_t blob_length)
{
    HRESULT hr;

    memset(pipeline_library, 0, sizeof(*pipeline_library));
    pipeline_library->ID3D12PipelineLibrary_iface.lpVtbl = &d3d12_pipeline_library_vtbl;
    pipeline_library->refcount = 1;

    if (FAILED(hr = vkd3d_private_store_init(&pipeline_library->private_store)))
        goto fail;

    d3d12_device_add_ref(pipeline_library->device = device);
    return S_OK;

fail:
    return hr;
}

HRESULT d3d12_pipeline_library_create(struct d3d12_device *device, const void *blob,
        size_t blob_length, struct d3d12_pipeline_library **pipeline_library)
{
    struct d3d12_pipeline_library *object;
    HRESULT hr;

    if (!(object = vkd3d_malloc(sizeof(*object))))
        return E_OUTOFMEMORY;

    if (FAILED(hr = d3d12_pipeline_library_init(object, device, blob, blob_length)))
    {
        vkd3d_free(object);
        return hr;
    }

    TRACE("Created pipeline library %p.\n", object);

    *pipeline_library = object;
    return S_OK;
}
