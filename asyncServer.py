import asyncio
import aiosqlite
import aiofiles
import os
import struct
import ssl
from collections import defaultdict
import time

HOST = '0.0.0.0'
PORT = 5000


LOCAL_CERT = True
CERTIFICATE_FILE = 'Encryption/CertificatesAndConfigs/server.crt' #Update these to your actual certificate paths
PRIVATE_KEY_FILE = 'Encryption/CertificatesAndConfigs/server.key' #Update these to your actual certificate paths

ssl_context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
ssl_context.load_cert_chain(certfile=CERTIFICATE_FILE, keyfile=PRIVATE_KEY_FILE)

clients = {}  # writer -> username (None until set)

clients_lock = asyncio.Lock()
file_lock = asyncio.Lock()

#The following variables are intended to configure my very basic rate limiting implementation
rate_lock = asyncio.Lock()
ENABLE_RATE_LIMITING = False 
rate_limits = {} # Tracks last messages per client IP or user
#These two function together, ie. you can send 8 requests in any 10 second time span:
MAX_REQUESTS = 8     # max allowed messages
TIME_WINDOW = 10         # in seconds

soft_ban = {} # addr: [time of ban, [blocked users]]
full_ban = {} # addr: time of ban
ban_duration = 30 # in seconds

#region COMMS Functions

async def send_message(writer: asyncio.StreamWriter, message: str):
    message_bytes = message.encode('utf-8')
    msglen = struct.pack('>I', len(message_bytes))
    writer.write(msglen + message_bytes)
    await writer.drain()  # Ensure the data is sent
    print("SENT")

async def recvall(reader: asyncio.StreamReader, n: int) -> bytes:
    #print("RECV")
    data = bytearray()
    while len(data) < n:
        packet = await reader.read(n - len(data))
        if not packet:
            return None
        data.extend(packet)
    return data


#endregion

#region DB and Auth

async def ensure_db():
    async with aiosqlite.connect("users.db") as db:
        cursor = await db.cursor()
        await cursor.execute("""
           CREATE TABLE IF NOT EXISTS users (
                username TEXT PRIMARY KEY,
                password TEXT NOT NULL,
                logged_in INTEGER DEFAULT 0,
                playing INTEGER DEFAULT 0,
                pts INTEGER DEFAULT 0
           )
                """)
        await db.commit()
        await db.close()
    async with aiosqlite.connect("users.db") as db:
        cursor = await db.cursor()
        logged_on_default = 0
        playing_default = 0

        await cursor.execute('''
            UPDATE users
            SET logged_in = ?,
                playing = ?
        ''', (logged_on_default, playing_default))

        await db.commit()
        await db.close()

async def register_user(username, password):
    async  with aiosqlite.connect("users.db") as conn:
        cursor = await conn.cursor()
        await cursor.execute('''
         CREATE TABLE IF NOT EXISTS users (
             username TEXT PRIMARY KEY,
             password TEXT NOT NULL,
             logged_in INTEGER DEFAULT 0,
             playing INTEGER DEFAULT 0
         )
         ''')
        await conn.commit()
        await cursor.execute('SELECT * FROM users WHERE username = ?', (username,))
        if await cursor.fetchone():
            print(f"-Username '{username}' already exists.-")
            await conn.close()
            return False
        else:

            await cursor.execute('INSERT INTO users (username, password, logged_in) VALUES (?, ?, ?)',
                           (username, password, 1))
            await conn.commit()
            print(f"-User '{username}' registered successfully.-")
            await conn.close()
            return True

async def login_user(username, password):
    async with aiosqlite.connect("users.db") as conn:
        cursor = await conn.cursor()
        await cursor.execute("""
           CREATE TABLE IF NOT EXISTS users (
                username TEXT PRIMARY KEY,
                password TEXT NOT NULL,
                logged_in INTEGER DEFAULT 0,
                playing INTEGER DEFAULT 0,
                pts INTEGER DEFAULT 0
           )
                """)
        await conn.commit()

        await cursor.execute('SELECT * FROM users WHERE username = ? AND password = ? AND logged_in = 0', (username, password))
        user = await cursor.fetchone()

        if user is not None:
            await cursor.execute('UPDATE users SET logged_in = 1 WHERE username = ?', (username,))
            await conn.commit()
            await conn.close()
            #print("login successful")
            return True
        else:
            await conn.close()
            print("*invalid username or password or user already logged in*")
            return False

async def logout_user(username):
    async with aiosqlite.connect("users.db") as conn:
       cursor = await conn.cursor()
       await cursor.execute('UPDATE users SET logged_in = 0 WHERE username = ?', (username,))
       await cursor.execute('UPDATE users SET playing = 0 WHERE username = ?', (username,))

       await conn.commit()
       await conn.close()


    user_list = await online_list()
    print(f"[CURRENT USERS LOGGED]:{len(user_list)}:{user_list}")

#endregion

#region Resource Collection
async def online_list():
    async with aiosqlite.connect("users.db") as conn:
        cursor = await conn.cursor()
        await cursor.execute('SELECT username, pts FROM users WHERE logged_in = ? AND playing = ?', (1, 0))
        usernames = await cursor.fetchall()

        await conn.close()
    ret_str = ""
    usernames = [username[0] + ":" + str(username[1]) for username in usernames]

    user_points = []
    for item in usernames:
        username, points = item.split(':')
        user_points.append((username, int(points)))

    user_points.sort(key=lambda x: x[1], reverse=True)

    sorted_list = [f"{username}:{points}" for username, points in user_points]

    for item in sorted_list:
        ret_str += str(item.split(':')[0]) + "|"
    return ret_str

async def update_manifest(mode="manifest"):
    roms_list = await asyncio.to_thread(os.listdir, "serverDATA/roms")

    if mode == "manifest":
        return "!".join(roms_list) + "!"



#endregion

async def handle_client(reader: asyncio.StreamReader, writer: asyncio.StreamWriter):
    addr = writer.get_extra_info('peername')

    print(f"[NEW CONNECTION] {addr} connected.")

    connected = True
    #for those connecting
    if addr[0] in full_ban.keys():
        if time.time() - full_ban[addr[0]] > ban_duration:
            del full_ban[addr[0]]
        else: connected = False


    user = ""
    passw = ""
    async with clients_lock:
        clients[writer] = "NoName"
        rate_limits[addr[0]] = {addr[1]:0} #{IP: port: mes_count, port2:mes_count2}
    ten_time = time.time()
    #print(f"RATES:{rate_limits}")
    #print(list(rate_limits[addr[0]].keys()))
    last_message_count = [0,(time.time()) *3]
    while connected:

        try:
            raw_msglen = await recvall(reader, 4)
            if not raw_msglen:
                break
            msglen = struct.unpack('>I', raw_msglen)[0]
            #print(msglen)
            message = await recvall(reader, msglen)
            message = message.decode()
            if message:
                #print("MSG")
                if ENABLE_RATE_LIMITING:
                    if addr[0] in soft_ban.keys():

                        if time.time() - soft_ban[addr[0]][0] > ban_duration:
                            del soft_ban[addr[0]]
                    #for those already logged in
                    if addr[0] in full_ban.keys():
                        print("THOU IS GETTING FULL BANNED")
                        if time.time() - full_ban[addr[0]] > ban_duration:
                            del full_ban[addr[0]]
                        else:
                            await send_message(writer, "BAN")
                            connected = False
                            break
                    async with rate_lock:

                        current_time = time.time()
                        if rate_limits[addr[0]][addr[1]]+1 >= MAX_REQUESTS:
                            if current_time - ten_time < TIME_WINDOW:
                                print(f"[RATE LIMIT] Too many requests from {addr}")
                                await send_message(writer, "Error|Rate limit exceeded. Please slow down. You will be disconnected for 30 seconds")
                                #Problem with rate limit banning
                                bad_count = 0
                                for user_num in range(len(list(rate_limits[addr[0]].values()))):
                                    if rate_limits[addr[0]][list(rate_limits[addr[0]].keys())[user_num]] +1 >= MAX_REQUESTS:
                                        bad_count+=1
                                if bad_count >= len(list(rate_limits[addr[0]].values()))/2:
                                    #Ip ban
                                    full_ban[addr[0]] = time.time()
                                    pass
                                else:
                                    if addr[0] in soft_ban.keys():
                                        soft_ban[addr[0]][1].append(user)
                                    else:
                                        soft_ban[addr[0]] = [time.time(), [user]]

                                connected = False
                                break

                        if current_time - last_message_count[1] > TIME_WINDOW:
                            if rate_limits[addr[0]][addr[1]] == last_message_count[0]:
                                await send_message(writer, "Timeout, kick")
                                connected = False
                                break

                        if current_time - ten_time > TIME_WINDOW:
                            ten_time = current_time
                            rate_limits[addr[0]][addr[1]] = 0
                        rate_limits[addr[0]][addr[1]] += 1
                        last_message_count = [rate_limits[addr[0]][addr[1]] , time.time()]

                if message != "check":
                    print(f"[{user}] {message}")

                if message.startswith("REG"):
                    if addr[0] in soft_ban.keys():
                        connected = False
                        break
                    success = await register_user(message.split('|')[1], message.split('|')[2])

                    if success:
                        print(f"-New User {message.split('|')[1]} created-")

                        user_list = await online_list()

                        for user_sock in clients.keys():
                            await send_message(user_sock, f"Online user list|{user_list}")


                        await send_message(writer, f"User created Successfully|{message.split('|')[1]}")
                        user = message.split('|')[1]
                        async with clients_lock:
                            clients[writer] = user
                        passw = message.split('|')[2]
                    else:
                        await send_message(writer, "Error in user creation")

                if message.startswith("LOG"):
                    if addr[0] in soft_ban.keys():
                        if message.split('|')[1] in soft_ban[addr[0]][1]:
                            connected = False
                            break
                    success = await login_user(message.split('|')[1], message.split("|")[2])
                    # print(success)
                    if success:
                        print(f"-User {message.split('|')[1]} has logged in-")

                        user_list = await online_list()

                        for user_sock in clients.keys():
                            await send_message(user_sock, f"Online user list|{user_list}")

                        await send_message(writer, f"User Logged in Successfully|{message.split('|')[1]}")
                        user = message.split('|')[1]
                        async with clients_lock:
                            clients[writer] = user
                        passw = message.split('|')[2]
                    else:
                        await send_message(writer, "Error in user login")

                if message.startswith("MAN"):
                    game_list = await update_manifest("manifest")
                    # print("gameLIST:" + game_list)
                    await send_message(writer, f"Online game list|{game_list}")
                    # print("gameLIST sent")

                if message.startswith("BACK"):
                    to_go = message.split('|')[1]
                    #print(f"TOGO: {to_go} USER: {user}")
                    if to_go == "MEN":
                        async with clients_lock:
                            clients[writer] = "NoName"
                        await logout_user(user)
                        user = "NoName"

                if message.startswith("GAME"):
                    game_name = message.split("|")[1]
                    print(f"[ACQUIRING GAME DATA: {game_name}]")

                    async with file_lock:
                        async with aiofiles.open(f"serverDATA/roms/{game_name}", "rb") as game_file:
                            game_data = await game_file.read()

                    await send_message(writer, f"GAMEDATA|{game_data}")

                if message.startswith("INFO"):
                    try:
                        game_name = message.split('|')[1].split('.')[0]

                        async with file_lock:
                            async with aiofiles.open(f"serverDATA/info_pages/Text/{game_name}", "r") as f:
                                game_info_data = await f.read()
                                game_info = game_info_data.split(',')

                        game_pic = b""
                        art_dir = "serverDATA/info_pages/Art"
                        for f_name in os.listdir(art_dir):
                            if f_name.startswith(game_name):
                                async with file_lock:
                                    async with aiofiles.open(f"{art_dir}/{f_name}", "rb") as f:
                                        game_pic = await f.read()
                                    game_name = f_name
                                break

                        await send_message(writer,
                                           f"INFO||{game_name}||{game_info[0]}||{game_info[1]}||{game_pic}")
                    except Exception as e:
                        print(f"*ERROR LOADING INFO PACKET: {e}*")

                if message.startswith("QUITGAME"):
                    #comments cause I need to close the region
                    # to make this cleanerrr
                    raise Exception

            else:
                connected = False


        except:
            print(f"*error handling client {user}*")
            connected = False
    if writer:
        try:
            async with clients_lock:
                del clients[writer]
            async with rate_lock:
                del rate_limits[addr[0]][addr[1]]
                if len(rate_limits[addr[0]].keys()) == 0:
                    del rate_limits[addr[0]] # del ip sector if no users of ip are in
            writer.close()
            await writer.wait_closed()
            print(f"[CLOSE] socket closed for {user}")
        except Exception as e:
            print(f"[ERROR] while closing {e}")
        await logout_user(user)
    print(f"[DICON] {user} disconnected")


async def main():
    await ensure_db()

    conn_args = {

    'host': HOST,
    'port': PORT,
    'reuse_address':True # Equivalent to SO_REUSEADDR
    }

    if LOCAL_CERT:
        conn_args['ssl'] = ssl_context

    server = await asyncio.start_server(
        handle_client,
        **conn_args
    )
    print("[SERVER STARTED] Listening on port 5000")
    async with server:
        await server.serve_forever()

if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("It's ok")
