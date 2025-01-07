#![allow(non_snake_case)]
#![allow(non_upper_case_globals)]
#![allow(private_interfaces)]
#![allow(unused_attributes)]
#![feature(extern_types)]
#![feature(c_size_t)]

use std::{io::Write, net::TcpStream};

pub const VERSION:             u16 = 1;
pub const PACKET_LOGIN:        u16 = 0;
pub const PACKET_POSITION:     u16 = 1;
pub const PACKET_SETGRID:      u16 = 2;
pub const PACKET_SETCLIENTS:   u16 = 3;
pub const PACKET_LOGINSUCCESS: u16 = 4;
pub const PACKET_SETHELDCELL:  u16 = 5;
pub const PACKET_SETCELL:      u16 = 6;
pub const PACKET_COUNT:        u16 = 7;

#[repr(C)]
pub struct Color {
    pub r: u8,
    pub g: u8,
    pub b: u8
}

#[derive(Debug)]
pub struct Client {
    pub stream: TcpStream,
    pub id: u16,
    pub dead: bool,
    pub color: [u8; 3],
    pub x: f32,
    pub y: f32,
    pub name: [u8; 17] // Null terminated,
}

#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct CClient {
    pub id: u16,
    pub r: u8,
    pub g: u8,
    pub b: u8,
    pub x: f32,
    pub y: f32,
    pub name: [u8; 16 + 1]
}

impl CClient {
    pub fn to_be_bytes(&self) -> Vec<u8> {
        let mut bytes: Vec<u8> = Vec::new();
        bytes.extend(self.id.to_be_bytes());
        bytes.push(self.r);
        bytes.push(self.g);
        bytes.push(self.b);
        bytes.extend(self.x.to_be_bytes());
        bytes.extend(self.y.to_be_bytes());
        bytes.extend(self.name);
        bytes
    }
}

pub unsafe fn write_header(stream: &mut TcpStream, type2: u16, len: u16) {
    stream.write(&6969_u16.to_be_bytes()).unwrap();
    stream.write(&VERSION.to_be_bytes()).unwrap();
    stream.write(&type2.to_be_bytes()).unwrap();
    stream.write(&len.to_be_bytes()).unwrap();
}

pub unsafe fn write_login(stream: &mut TcpStream, name: &str, color: Color) {
    assert!(name.len() <= 16);

    let mut name_real = [0_u8; 16];
    let mut i = 0;
    for c in name.as_bytes() {
        name_real[i] = *c;
        i += 1;
    }

    stream.write(&name_real).unwrap();
    stream.write(&[color.r]).unwrap();
    stream.write(&[color.g]).unwrap();
    stream.write(&[color.b]).unwrap();
}

pub unsafe fn write_position(stream: &mut TcpStream, id: u16, x: f32, y: f32) {
    write_header(stream, PACKET_POSITION, 10);
    stream.write(&id.to_be_bytes()).unwrap();
    stream.write(&x.to_be_bytes()).unwrap();
    stream.write(&y.to_be_bytes()).unwrap();
}

pub unsafe fn write_setheldcell(stream: &mut TcpStream, id: u16, cell: &str) {
    write_header(stream, PACKET_SETHELDCELL, 4 + cell.len() as u16);
    stream.write(&id.to_be_bytes()).unwrap();
    stream.write(&(cell.len() as u16).to_be_bytes()).unwrap();
    stream.write(&cell.as_bytes()).unwrap();
}

pub unsafe fn write_setcell(stream: &mut TcpStream, id: String, rot: u8, x: i32, y: i32) {
    write_header(stream, PACKET_SETCELL, 11 + id.len() as u16);
    stream.write(&[rot]).unwrap();
    stream.write(&x.to_be_bytes()).unwrap();
    stream.write(&y.to_be_bytes()).unwrap();
    stream.write(&(id.len() as u16).to_be_bytes()).unwrap();
    stream.write(&id.as_bytes()).unwrap();
}


pub mod tsc {
    use std::ffi::{c_char, c_schar, c_int};
    use core::ffi::c_size_t;

    #[repr(C)]
    pub struct TscBuffer {
        pub mem: *mut std::ffi::c_char,
        pub len: c_size_t,
        pub cap: c_size_t
    }

    #[repr(C)]
    pub struct TscCell {
        pub id: *const c_char,
        pub texture: *const c_char,
        pub data: *mut TscCellReg,
        pub celltable: *mut TscCellTable,
        pub flags: c_size_t,
        pub lx: c_int,
        pub ly: c_int,
        pub rot: c_char,
        pub addedRot: c_schar,
        pub updated: bool
    }

    unsafe extern "C" {
        pub type TscGrid;
        pub type TscResourcePack;
        pub type TscCellReg;
        pub type TscCellTable;

        pub static currentGrid: *mut TscGrid;
        pub unsafe fn tsc_saving_newBuffer(initial: *const c_char) -> TscBuffer;
        pub unsafe fn tsc_saving_deleteBuffer(buffer: TscBuffer);
        pub unsafe fn tsc_saving_encodeWithSmallest(buffer: *mut TscBuffer, grid: *mut TscGrid);

        pub unsafe fn workers_setupBest();
        pub unsafe fn tsc_init_builtin_ids();
        pub unsafe fn tsc_subtick_addCore();
        pub unsafe fn tsc_saving_registerCore();
        pub unsafe fn tsc_createGrid(id: *const c_char, width: i32, height: i32, title: *const c_char, description: *const c_char) -> *mut TscGrid;
        pub unsafe fn tsc_switchGrid(grid: *mut TscGrid);
        pub unsafe fn tsc_strintern(str: *const c_char) -> *const c_char;
        pub unsafe fn tsc_createResourcePack(id: *const c_char) -> *mut TscResourcePack;
        pub unsafe fn tsc_getResourcePack(id: *const c_char) -> *mut TscResourcePack;
        pub unsafe fn tsc_enableResourcePack(pack: *mut TscResourcePack);
        pub unsafe fn tsc_saving_decodeWithAny(code: *const c_char, grid: *mut TscGrid);
        pub unsafe fn tsc_cell_create(id: *const c_char, rot: c_char) -> TscCell;
        pub unsafe fn tsc_grid_set(grid: *mut TscGrid, x: c_int, y: c_int, cell: *mut TscCell);
    }
}