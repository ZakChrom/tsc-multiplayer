#![allow(non_snake_case)]
#![allow(non_upper_case_globals)]
#![allow(private_interfaces)]
#![allow(unused_attributes)]
#![feature(extern_types)]

extern crate shared;
use std::{ffi::{c_char, c_int, c_void, CStr, CString}, io::Read, net::{Shutdown, TcpStream}, ptr::addr_of_mut, sync::mpsc::{Receiver, Sender}, thread};
use std::convert::TryInto;
use crate::shared::{
    CClient, Color,
    VERSION, PACKET_LOGIN, PACKET_POSITION, PACKET_SETGRID, PACKET_SETCLIENTS, PACKET_LOGINSUCCESS, PACKET_SETHELDCELL, PACKET_SETCELL, PACKET_COUNT,
    write_header, write_login, write_setheldcell, write_position, write_setcell
};
use crate::shared::tsc::*;

#[derive(Debug)]
pub struct ClientData { // I cant be bothered to update the CClient struct and update the c side so
    pub held: String
}

static mut tsc_client: Option<TcpStream> = None;
static mut tsc_clients: Vec<(CClient, ClientData)> = Vec::new();
static mut tsc_clientId: u16 = 0;

pub enum Message {
    Grid(Vec<u8>),
    Clients(Vec<(CClient, ClientData)>),
    SetPosition(u16, f32, f32),
    SetHeldCell(u16, String),
    SetCell(String, u8, i32, i32)
}

// pub struct NetworkCell {
//     pub id: String,
//     pub rot: u8,
//     pub added_rot: i8,
//     pub flags: u64
// }

#[no_mangle]
pub unsafe extern "C" fn tsc_mp_processMessage(reciever: *mut c_void) {
    let receiver = (reciever as *mut Receiver<Message>).as_mut().unwrap();
    loop {
        match receiver.try_recv() {
            Ok(message) => {
                match message {
                    Message::Grid(grid) => {
                        tsc_saving_decodeWithAny(grid.as_ptr() as *const i8, currentGrid);
                    },
                    Message::Clients(clients) => {
                        println!("Set clients {}", clients.len());
                        tsc_clients = clients;
                    },
                    Message::SetPosition(id, x, y) => {
                        'a: for client in addr_of_mut!(tsc_clients).as_mut().unwrap() {
                            if client.0.id == id {
                                client.0.x = x;
                                client.0.y = y;
                                break 'a;
                            }
                        }
                    },
                    Message::SetHeldCell(id, cell) => {
                        'a: for client in addr_of_mut!(tsc_clients).as_mut().unwrap() {
                            if client.0.id == id {
                                client.1.held = cell;
                                break 'a;
                            }
                        }
                    },
                    Message::SetCell(cell, rot, x, y) => {
                        let c = CString::new(cell).unwrap().into_raw();
                        // TODO: i think if c is a new string it wont copy it but will store it so strintern
                        // will return that and then gets put into the grid and then here gets freed
                        let c2 = tsc_strintern(c);
                        let mut cell = tsc_cell_create(c2, rot.try_into().unwrap());

                        tsc_grid_set(currentGrid, x, y, &mut cell as *mut TscCell);
                        drop(CString::from_raw(c));
                    }
                }
            },
            Err(_) => return
        }
    }
}

#[no_mangle]
pub unsafe extern "C" fn tsc_mp_connectToServer(server: *const c_char, name: *const c_char, color: Color) -> *mut c_void {
    assert!(tsc_client.is_none());

    let server = CStr::from_ptr(server).to_str().unwrap();
    println!("Trying to connect to: {}", server);

    tsc_client = Some(TcpStream::connect(server).unwrap());
    let name = CStr::from_ptr(name).to_str().unwrap();
    assert!(name.len() <= 16);

    let client = addr_of_mut!(tsc_client).as_mut().unwrap();
    let mut writer = client.as_mut().unwrap().try_clone().unwrap();
    let mut reader = client.as_ref().unwrap().try_clone().unwrap();

    write_header(&mut writer, PACKET_LOGIN, 19);
    write_login(&mut writer, name, color);

    let channel: (Sender<Message>, Receiver<Message>) = std::sync::mpsc::channel();
    let thing = Box::leak(Box::new(channel.1));

    thread::Builder::new().name("client".to_string()).spawn(move || {
        let sender = channel.0;
        loop {
            let mut buffer: [u8; 8] = [0; 8];
            reader.read_exact(&mut buffer).unwrap();
            
            let magic = u16::from_be_bytes(buffer[0..2].try_into().unwrap());
            let version = u16::from_be_bytes(buffer[2..4].try_into().unwrap());
            let type2 = u16::from_be_bytes(buffer[4..6].try_into().unwrap());
            let len = u16::from_be_bytes(buffer[6..8].try_into().unwrap());
        
            if magic != 6969 || version != VERSION || type2 >= PACKET_COUNT {
                reader.shutdown(Shutdown::Both).unwrap();
                return
            }

            let mut packet: Vec<u8> = vec![0; len as usize];
            reader.read_exact(&mut packet).unwrap();
            assert!(packet.len() == len as usize);
    
            let code = packet.clone();

            match type2 {
                PACKET_SETGRID => {
                    sender.send(Message::Grid(code)).unwrap();
                },
                PACKET_SETCLIENTS => {
                    sender.send(Message::Clients(packet.chunks(30).map(|e| {
                        (
                            CClient {
                                id: u16::from_be_bytes(e[0..2].try_into().unwrap()),
                                r: e[2],
                                g: e[3],
                                b: e[4],
                                x: f32::from_be_bytes(e[5..9].try_into().unwrap()),
                                y: f32::from_be_bytes(e[9..13].try_into().unwrap()),
                                name: e[13..30].try_into().unwrap()
                            },
                            ClientData {
                                held: "fallback".to_string()
                            }
                        )
                    }).collect())).unwrap();
                },
                PACKET_LOGINSUCCESS => {
                    tsc_clientId = u16::from_be_bytes(packet[0..2].try_into().unwrap());
                    write_setheldcell(&mut writer, tsc_clientId, "mover");
                },
                PACKET_POSITION => {
                    sender.send(Message::SetPosition(
                        u16::from_be_bytes(packet[0..2].try_into().unwrap()),
                        f32::from_be_bytes(packet[2..6].try_into().unwrap()),
                        f32::from_be_bytes(packet[6..10].try_into().unwrap())
                    )).unwrap();
                },
                PACKET_SETHELDCELL => {
                    let len = u16::from_be_bytes(packet[2..4].try_into().unwrap());
                    let cell = std::str::from_utf8(&packet[4..4+(len as usize)]).unwrap();
                    sender.send(Message::SetHeldCell(
                        u16::from_be_bytes(packet[0..2].try_into().unwrap()),
                        cell.to_string()
                    )).unwrap();
                },
                PACKET_SETCELL => {
                    let rot = packet[0];
                    let x = i32::from_be_bytes(packet[1..5].try_into().unwrap());
                    let y = i32::from_be_bytes(packet[5..9].try_into().unwrap());
                    let len = u16::from_be_bytes(packet[9..11].try_into().unwrap());
                    let cell = std::str::from_utf8(&packet[11..11+(len as usize)]).unwrap().to_string();
                    sender.send(Message::SetCell(cell, rot, x, y)).unwrap();
                }
                _ => panic!()
            }
        }
    }).unwrap();

    thing as *mut Receiver<Message> as *mut c_void
}

#[no_mangle]
pub unsafe extern "C" fn tsc_mp_getClientsLen() -> c_int {
    tsc_clients.len().try_into().unwrap()
}

#[no_mangle]
pub unsafe extern "C" fn tsc_mp_getClient(i: c_int) -> CClient {
    tsc_clients[i as usize].0
}

#[no_mangle]
pub unsafe extern "C" fn tsc_mp_isMultiplayer() -> bool {
    tsc_client.is_some()
}

#[no_mangle]
pub unsafe extern "C" fn tsc_mp_moveCursor(x: f32, y: f32) {
    let stream = addr_of_mut!(tsc_client).as_mut().unwrap().as_mut().unwrap();
    write_position(stream, tsc_clientId, x, y);
}

#[no_mangle]
pub unsafe extern "C" fn tsc_mp_getMyId() -> u16 {
    tsc_clientId
}

#[no_mangle]
pub unsafe extern "C" fn tsc_mp_getHeldCell(i: c_int) -> *mut c_char {
    let client = &tsc_clients[i as usize].1;
    CString::new(client.held.clone()).unwrap().into_raw()
}

#[no_mangle]
pub unsafe extern "C" fn tsc_mp_freeHeldCell(cell: *mut c_char) {
    drop(CString::from_raw(cell))
}

#[no_mangle]
pub unsafe extern "C" fn tsc_mp_placeCell(id: *const c_char, rot: c_char, x: c_int, y: c_int) {
    let stream = addr_of_mut!(tsc_client).as_mut().unwrap().as_mut().unwrap();
    write_setcell(stream, CStr::from_ptr(id).to_str().unwrap().to_string(), rot.try_into().unwrap(), x, y);
}
