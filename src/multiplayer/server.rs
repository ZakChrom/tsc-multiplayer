#![allow(non_snake_case)]
#![allow(non_upper_case_globals)]
#![allow(private_interfaces)]
#![feature(c_size_t)]
#![feature(extern_types)]

extern crate shared;
use std::{ffi::{CStr, CString}, io::{Read, Write}, net::{SocketAddr, TcpListener, TcpStream}, ptr::{addr_of, addr_of_mut}, sync::{Arc, RwLock, TryLockError}, thread};
use crate::shared::{
    Client, CClient,
    VERSION, PACKET_LOGIN, PACKET_POSITION, PACKET_SETGRID, PACKET_SETCLIENTS, PACKET_LOGINSUCCESS, PACKET_SETHELDCELL, PACKET_SETCELL, PACKET_COUNT,
    write_header, write_setheldcell, write_position, write_setcell
};
use crate::shared::tsc::*;

mod client;
static mut tsc_clients: Vec<Arc<RwLock<Client>>> = Vec::new();
static mut id_thing: u16 = 1;

fn main() { unsafe {
    workers_setupBest();
    tsc_init_builtin_ids();
    tsc_subtick_addCore();
    tsc_saving_registerCore();
    // let defaultRP = tsc_strintern(c"default".as_ptr());
    // tsc_createResourcePack(defaultRP);
    // let defaultResourcePack = tsc_getResourcePack(defaultRP);
    // tsc_enableResourcePack(defaultResourcePack);

    let grid = tsc_createGrid(c"main".as_ptr(), 256, 256, std::ptr::null(), std::ptr::null());
    tsc_switchGrid(grid);

    loop {
        let listener = TcpListener::bind("localhost:6969").unwrap();

        for stream in listener.incoming() {
            let stream = stream.unwrap();
            let addr = stream.peer_addr().unwrap();
            let client = Arc::new(RwLock::new(Client {
                stream: stream.try_clone().unwrap(),
                id: id_thing,
                dead: false,
                color: [255; 3],
                x: 0.0,
                y: 0.0,
                name: "undefined\0\0\0\0\0\0\0\0".as_bytes().try_into().unwrap()
                // name: [b'u', b'n', b'd', b'e', b'f', b'i', b'n', b'e', b'd', 0, 0, 0, 0, 0, 0, 0, 0]
            }));
            id_thing += 1;
    
            tsc_clients.push(client.clone());
            thread::Builder::new().name("client handler".to_string()).spawn(move || {
                let mut stream = stream;
                
                let ret = client_handler(client.clone(), &mut stream, addr);
                client.write().unwrap().dead = true;

                yeet_clients();

                let mut things: Vec<CClient> = Vec::new();
                for c in addr_of!(tsc_clients).as_ref().unwrap() {
                    let c = c.read().unwrap();
                    things.push(CClient {
                        id: c.id,
                        r: c.color[0],
                        g: c.color[1],
                        b: c.color[2],
                        x: c.x,
                        y: c.x,
                        name: c.name
                    });
                }

                for c in addr_of_mut!(tsc_clients).as_mut().unwrap() {
                    let mut c = c.write().unwrap();
                    write_setclients(&mut c.stream, &things);
                }

                if let Err(e) = ret {
                    panic!("{}", e);
                }
            }).unwrap();
            println!("New client from: {:?}", addr);
        }

        // match listener.accept() {
        //     Ok((stream, addr)) => {
        //     },
        //     Err(ref e) if e.kind() != std::io::ErrorKind::WouldBlock => {
        //         panic!("{:?}", e);
        //     },
        //     _ => {}
        // }

        yeet_clients();
    }
}}

unsafe fn yeet_clients() {
    tsc_clients.retain(|c| {
        match c.try_read() {
            Ok(c) => !c.dead,
            Err(e) => {
                match e {
                    TryLockError::Poisoned(_) => false,
                    TryLockError::WouldBlock => true,
                }
            }
        }
    });
}

fn client_handler(client: Arc<RwLock<Client>>, stream: &mut TcpStream, addr: SocketAddr) -> Result<(), Box<dyn std::error::Error>> {
    //let mut client = client.write().ok().ok_or(Box::new(std::io::Error::new(std::io::ErrorKind::InvalidInput, "")))?;
    loop {
        let mut buffer: [u8; 8] = [0; 8];
        stream.read_exact(&mut buffer)?;
        
        let magic = u16::from_be_bytes(buffer[0..2].try_into()?);
        let version = u16::from_be_bytes(buffer[2..4].try_into()?);
        let type2 = u16::from_be_bytes(buffer[4..6].try_into()?);
        let len = u16::from_be_bytes(buffer[6..8].try_into()?);
    
        if magic != 6969 || version != VERSION || type2 >= PACKET_COUNT {
            stream.shutdown(std::net::Shutdown::Both)?;
            client.write().unwrap().dead = true;
        }

        println!("{}: {}({})", addr, type2, len);
        
        let mut packet: Vec<u8> = vec![0; len as usize];
        stream.read_exact(&mut packet)?;
        assert!(packet.len() == len as usize);

        match type2 {
            PACKET_LOGIN => {
                if len != 19 {
                    stream.shutdown(std::net::Shutdown::Both)?;
                    client.write().unwrap().dead = true;
                    return Err(Box::new(std::io::Error::new(std::io::ErrorKind::InvalidInput, format!("invalid len: {}", len))))
                }

                std::str::from_utf8(&packet[0..16])?; // Check if its valid utf8
                let mut name = packet[0..16].to_vec();
                name.push(0);
                
                client.write().unwrap().name = name.as_slice().try_into()?;
                client.write().unwrap().color = [packet[16], packet[17], packet[18]];

                unsafe {
                    write_loginsuccess(stream, client.clone());
                    write_grid(stream);
                }
                
                let mut things: Vec<CClient> = Vec::new();
                unsafe {
                    for c in addr_of!(tsc_clients).as_ref().unwrap() {
                        let c = c.read().unwrap();
                        things.push(CClient {
                            id: c.id,
                            r: c.color[0],
                            g: c.color[1],
                            b: c.color[2],
                            x: c.x,
                            y: c.x,
                            name: c.name
                        });
                    }
                }

                unsafe {
                    for c in addr_of_mut!(tsc_clients).as_mut().unwrap() {
                        let mut c = c.write().unwrap();
                        write_setclients(&mut c.stream, &things);
                    }
                }
            },
            PACKET_POSITION => {
                if len != 10 {
                    stream.shutdown(std::net::Shutdown::Both)?;
                    client.write().unwrap().dead = true;
                    return Err(Box::new(std::io::Error::new(std::io::ErrorKind::InvalidInput, format!("invalid len: {}", len))))
                }
                
                let id = u16::from_be_bytes(packet[0..2].try_into()?);
                if id != client.read().unwrap().id {
                    stream.shutdown(std::net::Shutdown::Both)?;
                    client.write().unwrap().dead = true;
                    return Err(Box::new(std::io::Error::new(std::io::ErrorKind::InvalidInput, format!("invalid id {}. Expected {}", id, client.read().unwrap().id))))
                }

                let x = f32::from_be_bytes(packet[2..6].try_into()?);
                let y = f32::from_be_bytes(packet[6..10].try_into()?);
                client.write().unwrap().x = x;
                client.write().unwrap().y = y;

                unsafe {
                    let client = client.read().unwrap();
                    for c in addr_of_mut!(tsc_clients).as_mut().unwrap() {
                        if client.id == c.read().unwrap().id { continue; }

                        let mut c = c.write().unwrap();
                        write_position(&mut c.stream, client.id, client.x, client.y);
                    }
                }
            },
            PACKET_SETHELDCELL => {
                if len <= 3 {
                    stream.shutdown(std::net::Shutdown::Both)?;
                    client.write().unwrap().dead = true;
                    return Err(Box::new(std::io::Error::new(std::io::ErrorKind::InvalidInput, format!("invalid len: {}", len))))
                }

                let id = u16::from_be_bytes(packet[0..2].try_into()?);
                if id != client.read().unwrap().id {
                    stream.shutdown(std::net::Shutdown::Both)?;
                    client.write().unwrap().dead = true;
                    return Err(Box::new(std::io::Error::new(std::io::ErrorKind::InvalidInput, format!("invalid id {}. Expected {}", id, client.read().unwrap().id))))
                }

                let len = u16::from_be_bytes(packet[2..4].try_into()?);
                let cell = std::str::from_utf8(&packet[4..4+(len as usize)])?;

                unsafe {
                    let client = client.read().unwrap();
                    for c in addr_of_mut!(tsc_clients).as_mut().unwrap() {
                        if client.id == c.read()?.id { continue; }

                        let mut c = c.write()?;
                        write_setheldcell(&mut c.stream, client.id, cell);
                    }
                }
            },
            PACKET_SETCELL => {
                if len <= 11 {
                    stream.shutdown(std::net::Shutdown::Both)?;
                    client.write().unwrap().dead = true;
                    return Err(Box::new(std::io::Error::new(std::io::ErrorKind::InvalidInput, format!("invalid len: {}", len))))
                }

                let rot = packet[0];
                let x = i32::from_be_bytes(packet[1..5].try_into()?);
                let y = i32::from_be_bytes(packet[5..9].try_into()?);
                let len = u16::from_be_bytes(packet[9..11].try_into()?);
                let cell = std::str::from_utf8(&packet[11..11+(len as usize)])?.to_string();
                
                unsafe {
                    let c = CString::new(cell.clone()).unwrap().into_raw();
                    let c2 = tsc_strintern(c);
                    let mut c3 = tsc_cell_create(c2, rot.try_into().unwrap());
                    tsc_grid_set(currentGrid, x, y, &mut c3 as *mut TscCell);
                    drop(CString::from_raw(c));
                }

                unsafe {
                    let client = client.read().unwrap();
                    for c in addr_of_mut!(tsc_clients).as_mut().unwrap() {
                        if client.id == c.read()?.id { continue; }
                        let mut c = c.write()?;
                        write_setcell(&mut c.stream, cell.clone(), rot, x, y);
                    }
                }
            }
            _ => return Err(Box::new(std::io::Error::new(std::io::ErrorKind::InvalidInput, "")))
        }
    }
}

unsafe fn write_setclients(stream: &mut TcpStream, clients: &Vec<CClient>) {
    // TODO: Dont hardcode client size :staring_cat:
    write_header(stream, PACKET_SETCLIENTS, (30 * clients.len()).try_into().unwrap());
    for client in clients {
        let bytes = &client.to_be_bytes();
        assert!(bytes.len() == 30);
        stream.write(bytes).unwrap();
    }
}

unsafe fn write_grid(stream: &mut TcpStream) {
    let mut buffer = tsc_saving_newBuffer(c"".as_ptr());
    tsc_saving_encodeWithSmallest(addr_of_mut!(buffer), currentGrid);
    let grid = CStr::from_ptr(buffer.mem).to_str().unwrap();
    write_header(stream, PACKET_SETGRID, grid.len().try_into().unwrap());
    _ = stream.write(grid.as_bytes());

    tsc_saving_deleteBuffer(buffer);
}

unsafe fn write_loginsuccess(stream: &mut TcpStream, client: Arc<RwLock<Client>>) {
    write_header(stream, PACKET_LOGINSUCCESS, 2);
    stream.write(&client.read().unwrap().id.to_be_bytes()).unwrap();
}

// unsafe fn write_setposition(stream: &mut TcpStream, client: Arc<RwLock<Client>>) {
//     let client = client.read().unwrap();
//     write_header(stream, PACKET_POSITION, 10);
//     stream.write(&client.id.to_be_bytes()).unwrap();
//     stream.write(&client.x.to_be_bytes()).unwrap();
//     stream.write(&client.y.to_be_bytes()).unwrap();
// }

/*
Bugs:
ClientData is not send to the clients on SetClients
I couldnt be bothered to put the held cell into the Clients struct as i its a any size string so whatever
When you join everyone has the fallback cell as the default
*/

/*
Types:
    ClientId: non zero u16. zero means you dont have an id yet (not logged in)

    Network Cell:
        id: string
        rot: char
        addedRot: signed char
        flags: size_t (u64 skill issue)
        Maybe: texture: string
        
    Array: Length prefixed array of stuff
    Name: 16 char utf8 string (not null terminated)
    Color: 3 u8's (rgb)
    String: length prefixed utf8 string (not null terminated)
*/

/*
Usual packets sent and recieved:
    Login:
        C->S: Login
        If login fails it just yeets u :staring_cat:
        S->C: LoginSuccess
        S->C: SetClients (everyone)
        S->C: SetGrid (everyone, maybe it shouldnt be everyone but the grid may get desynced so it could be good to sync it here)

    Play:
        Cursor moved:
            C->S: SetPosition
            S->C: SetPosition (other clients)

        Cell held and placed:
            C->S: SetHeldCell (default is empty which isnt rendered)
            S->C: SetHeldCell (other clients)
            C->S: SetCell (if cell was placed)
            S->C: SetCell (other clients)
*/

/*
Login (C->S)
Name
Color
*/

/*
LoginSuccess
ClientId (the client who logind's id)
*/

/*
SetClients (S->C)
Array<Client>
*/

/*
SetHeldCell
ClientId (id of the person whos cell is getting set)
Id: string
*/

/*
SetPosition
ClientId (id of the person whos cursor's pos is being set)
X(f32)
Y(f32)
*/