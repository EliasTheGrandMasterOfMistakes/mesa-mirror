// Copyright 2025 Google
// SPDX-License-Identifier: MIT

//! Protocol based on ${crosvm_src}/devices/src/virtio/gpu/protocol.rs

use mesa3d_util::MesaHandle;
use zerocopy::FromBytes;
use zerocopy::Immutable;
use zerocopy::IntoBytes;

/// A unique identifier for a device.
#[derive(
    Copy,
    Clone,
    Debug,
    Default,
    PartialEq,
    Eq,
    PartialOrd,
    Ord,
    Hash,
    FromBytes,
    IntoBytes,
    Immutable,
)]
#[repr(C)]
pub struct DeviceId {
    pub device_uuid: [u8; 16],
    pub driver_uuid: [u8; 16],
}

/// Memory index and physical device id of the associated VkDeviceMemory.
#[derive(
    Copy,
    Clone,
    Debug,
    Default,
    PartialEq,
    Eq,
    PartialOrd,
    Ord,
    Hash,
    FromBytes,
    IntoBytes,
    Immutable,
)]
#[repr(C)]
pub struct VulkanInfo {
    pub memory_idx: u32,
    pub device_id: DeviceId,
}

/* 2d commands */
pub const KUMQUAT_GPU_PROTOCOL_RESOURCE_UNREF: u32 = 0x100;
pub const KUMQUAT_GPU_PROTOCOL_GET_NUM_CAPSETS: u32 = 0x101;
pub const KUMQUAT_GPU_PROTOCOL_GET_CAPSET_INFO: u32 = 0x102;
pub const KUMQUAT_GPU_PROTOCOL_GET_CAPSET: u32 = 0x103;
pub const KUMQUAT_GPU_PROTOCOL_RESOURCE_CREATE_BLOB: u32 = 0x104;

/* 3d commands */
pub const KUMQUAT_GPU_PROTOCOL_CTX_CREATE: u32 = 0x200;
pub const KUMQUAT_GPU_PROTOCOL_CTX_DESTROY: u32 = 0x201;
pub const KUMQUAT_GPU_PROTOCOL_CTX_ATTACH_RESOURCE: u32 = 0x202;
pub const KUMQUAT_GPU_PROTOCOL_CTX_DETACH_RESOURCE: u32 = 0x203;
pub const KUMQUAT_GPU_PROTOCOL_RESOURCE_CREATE_3D: u32 = 0x204;
pub const KUMQUAT_GPU_PROTOCOL_TRANSFER_TO_HOST_3D: u32 = 0x205;
pub const KUMQUAT_GPU_PROTOCOL_TRANSFER_FROM_HOST_3D: u32 = 0x206;
pub const KUMQUAT_GPU_PROTOCOL_SUBMIT_3D: u32 = 0x207;
pub const KUMQUAT_GPU_PROTOCOL_RESOURCE_MAP_BLOB: u32 = 0x208;
pub const KUMQUAT_GPU_PROTOCOL_RESOURCE_UNMAP_BLOB: u32 = 0x209;
pub const KUMQUAT_GPU_PROTOCOL_SNAPSHOT_SAVE: u32 = 0x208;
pub const KUMQUAT_GPU_PROTOCOL_SNAPSHOT_RESTORE: u32 = 0x209;

/* success responses */
pub const KUMQUAT_GPU_PROTOCOL_RESP_NODATA: u32 = 0x3001;
pub const KUMQUAT_GPU_PROTOCOL_RESP_NUM_CAPSETS: u32 = 0x3002;
pub const KUMQUAT_GPU_PROTOCOL_RESP_CAPSET_INFO: u32 = 0x3003;
pub const KUMQUAT_GPU_PROTOCOL_RESP_CAPSET: u32 = 0x3004;
pub const KUMQUAT_GPU_PROTOCOL_RESP_CONTEXT_CREATE: u32 = 0x3005;
pub const KUMQUAT_GPU_PROTOCOL_RESP_RESOURCE_CREATE: u32 = 0x3006;
pub const KUMQUAT_GPU_PROTOCOL_RESP_CMD_SUBMIT_3D: u32 = 0x3007;
pub const KUMQUAT_GPU_PROTOCOL_RESP_OK_SNAPSHOT: u32 = 0x3008;

#[derive(Copy, Clone, Debug, Default, FromBytes, IntoBytes, Immutable)]
#[repr(C)]
pub struct kumquat_gpu_protocol_ctrl_hdr {
    pub type_: u32,
    pub payload: u32,
}

#[derive(Copy, Clone, Debug, Default, FromBytes, IntoBytes, Immutable)]
#[repr(C)]
pub struct kumquat_gpu_protocol_box {
    pub x: u32,
    pub y: u32,
    pub z: u32,
    pub w: u32,
    pub h: u32,
    pub d: u32,
}

/* KUMQUAT_GPU_PROTOCOL_TRANSFER_TO_HOST_3D, KUMQUAT_GPU_PROTOCOL_TRANSFER_FROM_HOST_3D */
#[derive(Copy, Clone, Debug, Default, FromBytes, IntoBytes, Immutable)]
#[repr(C)]
pub struct kumquat_gpu_protocol_transfer_host_3d {
    pub hdr: kumquat_gpu_protocol_ctrl_hdr,
    pub box_: kumquat_gpu_protocol_box,
    pub offset: u64,
    pub level: u32,
    pub stride: u32,
    pub layer_stride: u32,
    pub ctx_id: u32,
    pub resource_id: u32,
    pub padding: u32,
}

/* KUMQUAT_GPU_PROTOCOL_RESOURCE_CREATE_3D */
#[derive(Copy, Clone, Debug, Default, FromBytes, IntoBytes, Immutable)]
#[repr(C)]
pub struct kumquat_gpu_protocol_resource_create_3d {
    pub hdr: kumquat_gpu_protocol_ctrl_hdr,
    pub target: u32,
    pub format: u32,
    pub bind: u32,
    pub width: u32,
    pub height: u32,
    pub depth: u32,
    pub array_size: u32,
    pub last_level: u32,
    pub nr_samples: u32,
    pub flags: u32,
    pub size: u32,
    pub stride: u32,
    pub ctx_id: u32,
}

#[derive(Clone, Debug, Copy, FromBytes, IntoBytes, Immutable)]
#[repr(C)]
pub struct kumquat_gpu_protocol_ctx_create {
    pub hdr: kumquat_gpu_protocol_ctrl_hdr,
    pub nlen: u32,
    pub context_init: u32,
    pub debug_name: [u8; 64],
}

impl Default for kumquat_gpu_protocol_ctx_create {
    fn default() -> Self {
        // SAFETY: All zero pattern is safe for this particular struct
        unsafe { ::std::mem::zeroed() }
    }
}

/* KUMQUAT_GPU_PROTOCOL_CTX_ATTACH_RESOURCE, KUMQUAT_GPU_PROTOCOL_CTX_DETACH_RESOURCE */
#[derive(Copy, Clone, Debug, Default, FromBytes, IntoBytes, Immutable)]
#[repr(C)]
pub struct kumquat_gpu_protocol_ctx_resource {
    pub hdr: kumquat_gpu_protocol_ctrl_hdr,
    pub ctx_id: u32,
    pub resource_id: u32,
}

/* KUMQUAT_GPU_PROTOCOL_SUBMIT_3D */
#[derive(Copy, Clone, Debug, Default, FromBytes, IntoBytes, Immutable)]
#[repr(C)]
pub struct kumquat_gpu_protocol_cmd_submit {
    pub hdr: kumquat_gpu_protocol_ctrl_hdr,
    pub ctx_id: u32,
    pub pad: u32,
    pub size: u32,

    // The in-fence IDs are prepended to the cmd_buf and memory layout
    // of the KUMQUAT_GPU_PROTOCOL_SUBMIT_3D buffer looks like this:
    //   _________________
    //   | CMD_SUBMIT_3D |
    //   -----------------
    //   |  header       |
    //   |  in-fence IDs |
    //   |  cmd_buf      |
    //   -----------------
    //
    // This makes in-fence IDs naturally aligned to the sizeof(u64) inside
    // of the virtio buffer.
    pub num_in_fences: u32,
    pub flags: u32,
    pub ring_idx: u8,
    pub padding: [u8; 3],
}

/* KUMQUAT_GPU_PROTOCOL_RESP_CAPSET_INFO */
#[derive(Copy, Clone, Debug, Default, FromBytes, IntoBytes, Immutable)]
#[repr(C)]
pub struct kumquat_gpu_protocol_resp_capset_info {
    pub hdr: kumquat_gpu_protocol_ctrl_hdr,
    pub capset_id: u32,
    pub version: u32,
    pub size: u32,
    pub padding: u32,
}

/* KUMQUAT_GPU_PROTOCOL_GET_CAPSET */
#[derive(Copy, Clone, Debug, Default, FromBytes, IntoBytes, Immutable)]
#[repr(C)]
pub struct kumquat_gpu_protocol_get_capset {
    pub hdr: kumquat_gpu_protocol_ctrl_hdr,
    pub capset_id: u32,
    pub capset_version: u32,
}

#[derive(Copy, Clone, Debug, Default, FromBytes, IntoBytes, Immutable)]
#[repr(C)]
pub struct kumquat_gpu_protocol_resource_create_blob {
    pub hdr: kumquat_gpu_protocol_ctrl_hdr,
    pub ctx_id: u32,
    pub blob_mem: u32,
    pub blob_flags: u32,
    pub padding: u32,
    pub blob_id: u64,
    pub size: u64,
}

#[derive(Copy, Clone, Debug, Default, FromBytes, IntoBytes, Immutable)]
#[repr(C)]
pub struct kumquat_gpu_protocol_resp_resource_create {
    pub hdr: kumquat_gpu_protocol_ctrl_hdr,
    pub resource_id: u32,
    pub handle_type: u32,
    pub vulkan_info: VulkanInfo,
}

#[derive(Copy, Clone, Debug, Default, FromBytes, IntoBytes, Immutable)]
#[repr(C)]
pub struct kumquat_gpu_protocol_resp_cmd_submit_3d {
    pub hdr: kumquat_gpu_protocol_ctrl_hdr,
    pub fence_id: u64,
    pub handle_type: u32,
    pub padding: u32,
}

/// A virtio gpu command and associated metadata specific to each command.
#[derive(Debug)]
pub enum KumquatGpuProtocol {
    OkNoData,
    GetNumCapsets,
    GetCapsetInfo(u32),
    GetCapset(kumquat_gpu_protocol_get_capset),
    CtxCreate(kumquat_gpu_protocol_ctx_create),
    CtxDestroy(u32),
    CtxAttachResource(kumquat_gpu_protocol_ctx_resource),
    CtxDetachResource(kumquat_gpu_protocol_ctx_resource),
    ResourceCreate3d(kumquat_gpu_protocol_resource_create_3d),
    TransferToHost3d(kumquat_gpu_protocol_transfer_host_3d, MesaHandle),
    TransferFromHost3d(kumquat_gpu_protocol_transfer_host_3d, MesaHandle),
    CmdSubmit3d(kumquat_gpu_protocol_cmd_submit, Vec<u8>, Vec<u64>),
    ResourceCreateBlob(kumquat_gpu_protocol_resource_create_blob),
    SnapshotSave,
    SnapshotRestore,
    RespNumCapsets(u32),
    RespCapsetInfo(kumquat_gpu_protocol_resp_capset_info),
    RespCapset(Vec<u8>),
    RespContextCreate(u32),
    RespResourceCreate(kumquat_gpu_protocol_resp_resource_create, MesaHandle),
    RespCmdSubmit3d(u64, MesaHandle),
    RespOkSnapshot,
}

pub enum KumquatGpuProtocolWrite<T: IntoBytes + FromBytes + Immutable> {
    Cmd(T),
    CmdWithHandle(T, MesaHandle),
    CmdWithData(T, Vec<u8>),
}
