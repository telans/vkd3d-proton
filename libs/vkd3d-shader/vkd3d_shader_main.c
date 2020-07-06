/*
 * Copyright 2017 Józef Kucia for CodeWeavers
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

#include "vkd3d_shader_private.h"

#include <stdio.h>

static void vkd3d_shader_dump_blob(const char *path, const char *prefix, const void *data, size_t size,
        unsigned int id, const char *ext)
{
    char filename[1024];
    FILE *f;

    snprintf(filename, ARRAY_SIZE(filename), "%s/vkd3d-shader-%s-%u.%s", path, prefix, id, ext);
    if ((f = fopen(filename, "wb")))
    {
        if (fwrite(data, 1, size, f) != size)
            ERR("Failed to write shader to %s.\n", filename);
        if (fclose(f))
            ERR("Failed to close stream %s.\n", filename);
    }
    else
    {
        ERR("Failed to open %s for dumping shader.\n", filename);
    }
}

void vkd3d_shader_dump_shader(enum vkd3d_shader_type type, const struct vkd3d_shader_code *shader)
{
    static LONG shader_id = 0;
    static bool enabled = true;
    const char *path;

    if (!enabled)
        return;

    if (!(path = getenv("VKD3D_SHADER_DUMP_PATH")))
    {
        enabled = false;
        return;
    }

    vkd3d_shader_dump_blob(path, shader_get_type_prefix(type), shader->code, shader->size,
                           InterlockedIncrement(&shader_id) - 1, "dxbc");
}

void vkd3d_shader_dump_spirv_shader(enum vkd3d_shader_type type, const struct vkd3d_shader_code *shader)
{
    static LONG shader_id = 0;
    static bool enabled = true;
    const char *path;

    if (!enabled)
        return;

    if (!(path = getenv("VKD3D_SHADER_DUMP_PATH")))
    {
        enabled = false;
        return;
    }

    vkd3d_shader_dump_blob(path, shader_get_type_prefix(type), shader->code, shader->size,
                           InterlockedIncrement(&shader_id) - 1, "spv");
}

struct vkd3d_shader_parser
{
    struct vkd3d_shader_desc shader_desc;
    struct vkd3d_shader_version shader_version;
    void *data;
    const DWORD *ptr;
};

static int vkd3d_shader_parser_init(struct vkd3d_shader_parser *parser,
        const struct vkd3d_shader_code *dxbc)
{
    struct vkd3d_shader_desc *shader_desc = &parser->shader_desc;
    int ret;

    if ((ret = shader_extract_from_dxbc(dxbc->code, dxbc->size, shader_desc)) < 0)
    {
        WARN("Failed to extract shader, vkd3d result %d.\n", ret);
        return ret;
    }

    if (!(parser->data = shader_sm4_init(shader_desc->byte_code,
            shader_desc->byte_code_size, &shader_desc->output_signature)))
    {
        WARN("Failed to initialize shader parser.\n");
        free_shader_desc(shader_desc);
        return VKD3D_ERROR_INVALID_ARGUMENT;
    }

    shader_sm4_read_header(parser->data, &parser->ptr, &parser->shader_version);
    return VKD3D_OK;
}

static void vkd3d_shader_parser_destroy(struct vkd3d_shader_parser *parser)
{
    shader_sm4_free(parser->data);
    free_shader_desc(&parser->shader_desc);
}

static int vkd3d_shader_validate_compile_args(const struct vkd3d_shader_compile_arguments *compile_args)
{
    if (!compile_args)
        return VKD3D_OK;

    if (compile_args->type != VKD3D_SHADER_STRUCTURE_TYPE_COMPILE_ARGUMENTS)
    {
        WARN("Invalid structure type %#x.\n", compile_args->type);
        return VKD3D_ERROR_INVALID_ARGUMENT;
    }

    switch (compile_args->target)
    {
        case VKD3D_SHADER_TARGET_SPIRV_VULKAN_1_0:
            break;
        default:
            WARN("Invalid shader target %#x.\n", compile_args->target);
            return VKD3D_ERROR_INVALID_ARGUMENT;
    }

    return VKD3D_OK;
}

int vkd3d_shader_compile_dxbc(const struct vkd3d_shader_code *dxbc,
        struct vkd3d_shader_code *spirv, unsigned int compiler_options,
        const struct vkd3d_shader_interface_info *shader_interface_info,
        const struct vkd3d_shader_compile_arguments *compile_args)
{
    struct vkd3d_shader_instruction instruction;
    struct vkd3d_dxbc_compiler *spirv_compiler;
    struct vkd3d_shader_scan_info scan_info;
    struct vkd3d_shader_parser parser;
    int ret;

    TRACE("dxbc {%p, %zu}, spirv %p, compiler_options %#x, shader_interface_info %p, compile_args %p.\n",
            dxbc->code, dxbc->size, spirv, compiler_options, shader_interface_info, compile_args);

    if (shader_interface_info && shader_interface_info->type != VKD3D_SHADER_STRUCTURE_TYPE_SHADER_INTERFACE_INFO)
    {
        WARN("Invalid structure type %#x.\n", shader_interface_info->type);
        return VKD3D_ERROR_INVALID_ARGUMENT;
    }

    if ((ret = vkd3d_shader_validate_compile_args(compile_args)) < 0)
        return ret;

    /* DXIL is handled externally through dxil-spirv. */
    if (shader_is_dxil(dxbc->code, dxbc->size))
    {
#ifdef HAVE_DXIL_SPV
        return vkd3d_shader_compile_dxil(dxbc, spirv, shader_interface_info, compile_args);
#else
        ERR("DXIL shader found, but DXIL support is not enabled in vkd3d.\n");
        return VKD3D_ERROR_INVALID_SHADER;
#endif
    }

    scan_info.type = VKD3D_SHADER_STRUCTURE_TYPE_SCAN_INFO;
    scan_info.next = NULL;
    if ((ret = vkd3d_shader_scan_dxbc(dxbc, &scan_info)) < 0)
        return ret;

    if ((ret = vkd3d_shader_parser_init(&parser, dxbc)) < 0)
        return ret;

    vkd3d_shader_dump_shader(parser.shader_version.type, dxbc);

    if (TRACE_ON())
        vkd3d_shader_trace(parser.data);

    if (!(spirv_compiler = vkd3d_dxbc_compiler_create(&parser.shader_version,
            &parser.shader_desc, compiler_options, shader_interface_info, compile_args, &scan_info)))
    {
        ERR("Failed to create DXBC compiler.\n");
        vkd3d_shader_parser_destroy(&parser);
        return VKD3D_ERROR;
    }

    while (!shader_sm4_is_end(parser.data, &parser.ptr))
    {
        shader_sm4_read_instruction(parser.data, &parser.ptr, &instruction);

        if (instruction.handler_idx == VKD3DSIH_INVALID)
        {
            WARN("Encountered unrecognized or invalid instruction.\n");
            vkd3d_dxbc_compiler_destroy(spirv_compiler);
            vkd3d_shader_parser_destroy(&parser);
            return VKD3D_ERROR_INVALID_ARGUMENT;
        }

        if ((ret = vkd3d_dxbc_compiler_handle_instruction(spirv_compiler, &instruction)) < 0)
            break;
    }

    if (ret >= 0)
        ret = vkd3d_dxbc_compiler_generate_spirv(spirv_compiler, spirv);

    if (ret == 0)
        vkd3d_shader_dump_spirv_shader(parser.shader_version.type, spirv);

    vkd3d_dxbc_compiler_destroy(spirv_compiler);
    vkd3d_shader_parser_destroy(&parser);
    return ret;
}

static bool vkd3d_shader_instruction_is_uav_read(const struct vkd3d_shader_instruction *instruction)
{
    enum VKD3D_SHADER_INSTRUCTION_HANDLER handler_idx = instruction->handler_idx;
    return (VKD3DSIH_ATOMIC_AND <= handler_idx && handler_idx <= VKD3DSIH_ATOMIC_XOR)
            || (VKD3DSIH_IMM_ATOMIC_ALLOC <= handler_idx && handler_idx <= VKD3DSIH_IMM_ATOMIC_XOR)
            || handler_idx == VKD3DSIH_LD_UAV_TYPED || handler_idx == VKD3DSIH_LD_UAV_TYPED_FEEDBACK
            || ((handler_idx == VKD3DSIH_LD_RAW || handler_idx == VKD3DSIH_LD_RAW_FEEDBACK) && instruction->src[1].reg.type == VKD3DSPR_UAV)
            || ((handler_idx == VKD3DSIH_LD_STRUCTURED || handler_idx == VKD3DSIH_LD_STRUCTURED_FEEDBACK) && instruction->src[2].reg.type == VKD3DSPR_UAV);
}

static void vkd3d_shader_scan_record_uav_read(struct vkd3d_shader_scan_info *scan_info,
        const struct vkd3d_shader_register *reg)
{
    assert(reg->idx[0].offset < VKD3D_SHADER_MAX_UNORDERED_ACCESS_VIEWS);
    scan_info->uav_flags[reg->idx[0].offset] |= VKD3D_SHADER_UAV_FLAG_READ_ACCESS;
}

static bool vkd3d_shader_instruction_is_uav_counter(const struct vkd3d_shader_instruction *instruction)
{
    enum VKD3D_SHADER_INSTRUCTION_HANDLER handler_idx = instruction->handler_idx;
    return handler_idx == VKD3DSIH_IMM_ATOMIC_ALLOC
            || handler_idx == VKD3DSIH_IMM_ATOMIC_CONSUME;
}

static void vkd3d_shader_scan_record_uav_counter(struct vkd3d_shader_scan_info *scan_info,
        const struct vkd3d_shader_register *reg)
{
    assert(reg->idx[0].offset < VKD3D_SHADER_MAX_UNORDERED_ACCESS_VIEWS);
    scan_info->uav_flags[reg->idx[0].offset] |= VKD3D_SHADER_UAV_FLAG_ATOMIC_COUNTER;
}

static void vkd3d_shader_scan_input_declaration(struct vkd3d_shader_scan_info *scan_info,
        const struct vkd3d_shader_instruction *instruction)
{
    const struct vkd3d_shader_dst_param *dst = &instruction->declaration.dst;

    if (dst->reg.type == VKD3DSPR_OUTCONTROLPOINT)
        scan_info->use_vocp = true;
}

static void vkd3d_shader_scan_sampler_declaration(struct vkd3d_shader_scan_info *scan_info,
        const struct vkd3d_shader_instruction *instruction)
{
    unsigned int sampler_index = instruction->declaration.dst.reg.idx[0].offset;
    if (instruction->flags & VKD3DSI_SAMPLER_COMPARISON_MODE)
    {
        assert(sampler_index < CHAR_BIT * sizeof(scan_info->sampler_comparison_mode_mask));
        scan_info->sampler_comparison_mode_mask |= 1u << sampler_index;
    }
}

static void vkd3d_shader_scan_instruction(struct vkd3d_shader_scan_info *scan_info,
        const struct vkd3d_shader_instruction *instruction)
{
    unsigned int i;

    switch (instruction->handler_idx)
    {
        case VKD3DSIH_DCL_INPUT:
            vkd3d_shader_scan_input_declaration(scan_info, instruction);
            break;
        case VKD3DSIH_DCL_SAMPLER:
            vkd3d_shader_scan_sampler_declaration(scan_info, instruction);
            break;
        default:
            break;
    }

    if (vkd3d_shader_instruction_is_uav_read(instruction))
    {
        for (i = 0; i < instruction->dst_count; ++i)
        {
            if (instruction->dst[i].reg.type == VKD3DSPR_UAV)
                vkd3d_shader_scan_record_uav_read(scan_info, &instruction->dst[i].reg);
        }
        for (i = 0; i < instruction->src_count; ++i)
        {
            if (instruction->src[i].reg.type == VKD3DSPR_UAV)
                vkd3d_shader_scan_record_uav_read(scan_info, &instruction->src[i].reg);
        }
    }

    if (vkd3d_shader_instruction_is_uav_counter(instruction))
        vkd3d_shader_scan_record_uav_counter(scan_info, &instruction->src[0].reg);
}

int vkd3d_shader_scan_patch_vertex_count(const struct vkd3d_shader_code *dxbc,
        unsigned int *patch_vertex_count)
{
    struct vkd3d_shader_instruction instruction;
    struct vkd3d_shader_parser parser;
    int ret;

    if (shader_is_dxil(dxbc->code, dxbc->size))
    {
        /* TODO */
        *patch_vertex_count = 0;
        return VKD3D_OK;
    }
    else
    {
        if ((ret = vkd3d_shader_parser_init(&parser, dxbc)) < 0)
            return ret;

        *patch_vertex_count = 0;

        while (!shader_sm4_is_end(parser.data, &parser.ptr))
        {
            shader_sm4_read_instruction(parser.data, &parser.ptr, &instruction);

            if (instruction.handler_idx == VKD3DSIH_INVALID)
            {
                WARN("Encountered unrecognized or invalid instruction.\n");
                vkd3d_shader_parser_destroy(&parser);
                return VKD3D_ERROR_INVALID_ARGUMENT;
            }

            if (instruction.handler_idx == VKD3DSIH_DCL_INPUT_CONTROL_POINT_COUNT)
            {
                *patch_vertex_count = instruction.declaration.count;
                break;
            }
        }

        vkd3d_shader_parser_destroy(&parser);
        return VKD3D_OK;
    }
}

int vkd3d_shader_scan_dxbc(const struct vkd3d_shader_code *dxbc,
        struct vkd3d_shader_scan_info *scan_info)
{
    struct vkd3d_shader_instruction instruction;
    struct vkd3d_shader_parser parser;
    int ret;

    TRACE("dxbc {%p, %zu}, scan_info %p.\n", dxbc->code, dxbc->size, scan_info);

    if (scan_info->type != VKD3D_SHADER_STRUCTURE_TYPE_SCAN_INFO)
    {
        WARN("Invalid structure type %#x.\n", scan_info->type);
        return VKD3D_ERROR_INVALID_ARGUMENT;
    }

    if (shader_is_dxil(dxbc->code, dxbc->size))
    {
        /* There is nothing interesting to scan. DXIL does this internally. */
        return VKD3D_OK;
    }
    else
    {
        if ((ret = vkd3d_shader_parser_init(&parser, dxbc)) < 0)
            return ret;

        memset(scan_info, 0, sizeof(*scan_info));

        while (!shader_sm4_is_end(parser.data, &parser.ptr))
        {
            shader_sm4_read_instruction(parser.data, &parser.ptr, &instruction);

            if (instruction.handler_idx == VKD3DSIH_INVALID)
            {
                WARN("Encountered unrecognized or invalid instruction.\n");
                vkd3d_shader_parser_destroy(&parser);
                return VKD3D_ERROR_INVALID_ARGUMENT;
            }

            vkd3d_shader_scan_instruction(scan_info, &instruction);
        }

        vkd3d_shader_parser_destroy(&parser);
        return VKD3D_OK;
    }
}

void vkd3d_shader_free_shader_code(struct vkd3d_shader_code *shader_code)
{
    if (!shader_code)
        return;

    vkd3d_free((void *)shader_code->code);
}

static void vkd3d_shader_free_root_signature_v_1_0(struct vkd3d_root_signature_desc *root_signature)
{
    unsigned int i;

    for (i = 0; i < root_signature->parameter_count; ++i)
    {
        const struct vkd3d_root_parameter *parameter = &root_signature->parameters[i];

        if (parameter->parameter_type == VKD3D_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE)
            vkd3d_free((void *)parameter->descriptor_table.descriptor_ranges);
    }
    vkd3d_free((void *)root_signature->parameters);
    vkd3d_free((void *)root_signature->static_samplers);

    memset(root_signature, 0, sizeof(*root_signature));
}

static void vkd3d_shader_free_root_signature_v_1_1(struct vkd3d_root_signature_desc1 *root_signature)
{
    unsigned int i;

    for (i = 0; i < root_signature->parameter_count; ++i)
    {
        const struct vkd3d_root_parameter1 *parameter = &root_signature->parameters[i];

        if (parameter->parameter_type == VKD3D_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE)
            vkd3d_free((void *)parameter->descriptor_table.descriptor_ranges);
    }
    vkd3d_free((void *)root_signature->parameters);
    vkd3d_free((void *)root_signature->static_samplers);

    memset(root_signature, 0, sizeof(*root_signature));
}

void vkd3d_shader_free_root_signature(struct vkd3d_versioned_root_signature_desc *desc)
{
    if (desc->version == VKD3D_ROOT_SIGNATURE_VERSION_1_0)
    {
        vkd3d_shader_free_root_signature_v_1_0(&desc->v_1_0);
    }
    else if (desc->version == VKD3D_ROOT_SIGNATURE_VERSION_1_1)
    {
        vkd3d_shader_free_root_signature_v_1_1(&desc->v_1_1);
    }
    else if (desc->version)
    {
        FIXME("Unknown version %#x.\n", desc->version);
        return;
    }

    desc->version = 0;
}

int vkd3d_shader_parse_input_signature(const struct vkd3d_shader_code *dxbc,
        struct vkd3d_shader_signature *signature)
{
    TRACE("dxbc {%p, %zu}, signature %p.\n", dxbc->code, dxbc->size, signature);

    return shader_parse_input_signature(dxbc->code, dxbc->size, signature);
}

struct vkd3d_shader_signature_element *vkd3d_shader_find_signature_element(
        const struct vkd3d_shader_signature *signature, const char *semantic_name,
        unsigned int semantic_index, unsigned int stream_index)
{
    struct vkd3d_shader_signature_element *e;
    unsigned int i;

    TRACE("signature %p, semantic_name %s, semantic_index %u, stream_index %u.\n",
            signature, debugstr_a(semantic_name), semantic_index, stream_index);

    e = signature->elements;
    for (i = 0; i < signature->element_count; ++i)
    {
        if (!ascii_strcasecmp(e[i].semantic_name, semantic_name)
                && e[i].semantic_index == semantic_index
                && e[i].stream_index == stream_index)
            return &e[i];
    }

    return NULL;
}

void vkd3d_shader_free_shader_signature(struct vkd3d_shader_signature *signature)
{
    TRACE("signature %p.\n", signature);

    vkd3d_free(signature->elements);
    signature->elements = NULL;
}

int vkd3d_shader_supports_dxil(void)
{
#ifdef HAVE_DXIL_SPV
    return 1;
#else
    return 0;
#endif
}
