import ast
import asyncio
import ssl
import struct
import sys
import random
from functools import partial
import asyncio.subprocess
from PyQt5 import uic
from PyQt5.QtCore import QObject, Qt
from PyQt5.QtGui import QPixmap, QImage, QColor, QIcon
from PyQt5.QtWidgets import QApplication, QMainWindow, QWidget, QLineEdit, QPushButton, QVBoxLayout, QHBoxLayout, \
    QLabel, QScrollArea, QSizePolicy, QFrame, QTextEdit, QTextBrowser
from qasync import QEventLoop, asyncSlot
from pathlib import Path
#SERVER_HOST = 'XX.X.X.XX'
SERVER_PORT = 5000


#emulator_path = "GB-C-EmulatorV20.4/TW_emu/build/part1/gbemu/gbemu.exe"
emulator_path = "OneFileEmu/OneFileGBEMU.exe"
game_loop = False
discon = False
game_to_play = ""
graphics_lock = asyncio.Lock()

LOCAL_CERT = True
CERTIFICATE_FILE = 'Encryption/CertificatesAndConfigs/rootCA.crt'

app_stylesheet = ""
p = None #Game Process
cover_arts_path = Path('clientDATA/Main Page cover')
cover_arts = [f.name for f in cover_arts_path.iterdir() if f.is_file()]

# Create SSL context and load the server certificate
ssl_context = ssl.create_default_context(ssl.Purpose.SERVER_AUTH)
ssl_context.load_verify_locations(CERTIFICATE_FILE)

#region COMMS Functions

async def async_send_message(writer: asyncio.StreamWriter, message: str):
    global game_loop
    try:
        print("-Trying to check...-")
        message_bytes = message.encode('utf-8')
        msglen = struct.pack('>I', len(message_bytes))
        writer.write(msglen + message_bytes)
        await writer.drain()
        print("SENT")
    except (ConnectionResetError, BrokenPipeError, ConnectionError, ConnectionAbortedError, ConnectionRefusedError) as e:
        print(f"*Encountered ERROR while attempting to send message: {e}*")
        game_loop = False
        global p
        if p is not None:
            p.kill()
        writer.close()
        await writer.wait_closed()
        await discon_reset()
        print("killed")

async def async_recvall(reader: asyncio.StreamReader, n: int) -> bytes | None:
    print("RECV")
    data = bytearray()
    while len(data) < n:
        packet = await reader.read(n - len(data))
        if not packet:
            return None
        data.extend(packet)
    return bytes(data)


#endregion

#region Windows and Classes

class MenuWindow(QMainWindow):
    def __init__(self, writer, reader, parent=None):
        super().__init__(parent)
        uic.loadUi('UI/main_menu.ui', self)
        self.setFixedSize(1600, 700)
        self.username_line_edit = self.findChild(QLineEdit, 'lineEdit')
        self.password_line_edit = self.findChild(QLineEdit, 'lineEdit_2')
        self.pin_line_edit = self.findChild(QLineEdit, 'lineEdit_3')

        self.register_button = self.findChild(QPushButton, 'pushButton_5')
        self.login_button = self.findChild(QPushButton, 'pushButton_6')
        self.quit_button = self.findChild(QPushButton, 'pushButton_7')
        self.con_button = self.findChild(QPushButton, 'pushButton_8')
        self.credits_btn = self.findChild(QPushButton, "pushButtonINFO")

        self.cover_image = self.findChild(QLabel, "IMG_label")

        self.register_button.clicked.connect(self._wrap_async(self.register))
        self.login_button.clicked.connect(self._wrap_async(self.login))
        self.quit_button.clicked.connect(self._wrap_async(self.quit))
        self.con_button.clicked.connect(self._wrap_async(self.pin_connect))
        self.credits_btn.clicked.connect(self.cred_win)
        self.credits_btn.setEnabled(True)

        self.reader = reader
        self.writer = writer

    def _wrap_async(self, coro_func):
        def wrapper():
            asyncio.create_task(coro_func())

        return wrapper

    def cred_win(self):
        self.hide()
        credits_win.prev_window = "menu"
        credits_win.show()

    async def pin_connect(self):
        global HOST, PORT, communicate

        pin = str(self.pin_line_edit.text())
        if len(pin) > 16 and "ngrok" not in pin:
            print("Invalid PIN format.")
            window.con_button.setStyleSheet("background-color: red;")
            window.con_button.setEnabled(False)
            await asyncio.sleep(2)
            window.con_button.setStyleSheet(app_stylesheet)
            window.con_button.setEnabled(True)
            return

        #ngrok compatibility: uncomment to enable(currently using the eu forwarding link so you might need to change that to the link it gives you) 
        #HOST = pin[0] + ".tcp.eu.ngrok.io"
        # if "ngrok" in pin:
        #     pin = pin.split(':')
        #     HOST = pin[1][2:]
        #     PORT = pin[2]

        #     print(HOST)
        #     print(PORT)
        # else:
        HOST = pin
        PORT = SERVER_PORT
   

        await start_client()

    async def register(self):
        username = self.username_line_edit.text().replace("|", "[")
        password = self.password_line_edit.text()

        if len(username) < 3:
            if len(username) == 0:
                username = f"User{random.randrange(1, 9999)}"

        package = f"REG|{username}|{password}"
        await async_send_message(self.writer, package)

    async def login(self):
        username = self.username_line_edit.text().replace("|", "[")
        password = self.password_line_edit.text()

        if len(username) < 3:
            if len(username) == 0:
                username = f"User{random.randrange(1, 9999)}"

        package = f"LOG|{username}|{password}"
        await async_send_message(self.writer, package)

    async def quit(self):
        if self.writer is not None:
            await async_send_message(self.writer, "QUITGAME")
        QApplication.instance().quit()

class GameSelectWindow(QWidget):
    def __init__(self, username, writer, reader, parent=None):
        super().__init__(parent)
        self.username = username
        self.writer = writer
        self.reader = reader
        self.setWindowTitle("Game Selection")
        self.setGeometry(700, 400, 800, 600)
        self.init_ui()

    def init_ui(self):
        self.main_layout = QVBoxLayout()
        self.main_layout.setSpacing(0)
        self.main_layout.setContentsMargins(0, 0, 0, 0)
        #self.setFixedSize(1920,1080)
        # Top Bar
        top_bar = QHBoxLayout()
        top_bar.setContentsMargins(10, 10, 10, 10)

        self.username_label = QLabel(f"User: {self.username}")
        self.username_label.setAlignment(Qt.AlignLeft | Qt.AlignVCenter)

        title_label = QLabel("GAME SELECT")
        title_label.setAlignment(Qt.AlignCenter)
        title_label.setStyleSheet("font-weight: bold; font-size: 20px;")

        #account_button = QPushButton("Account")
        #account_button.setSizePolicy(QSizePolicy.Fixed, QSizePolicy.Fixed)

        top_bar.addWidget(self.username_label)
        top_bar.addStretch()
        top_bar.addWidget(title_label)
        top_bar.addStretch()
        #top_bar.addWidget(account_button)

        self.main_layout.addLayout(top_bar)

        # Scrollable Game List
        self.scroll_area = QScrollArea()
        self.scroll_area.setWidgetResizable(True)
        self.game_list_widget = QWidget()
        self.game_list_layout = QVBoxLayout()
        self.game_list_layout.setAlignment(Qt.AlignTop)
        self.game_list_widget.setLayout(self.game_list_layout)
        self.scroll_area.setWidget(self.game_list_widget)
        self.main_layout.addWidget(self.scroll_area)

        # Bottom Bar
        bottom_bar = QHBoxLayout()
        bottom_bar.setContentsMargins(10, 10, 10, 10)

        credits_button = QPushButton("Bio")
        back_button = QPushButton("Back")
        quit_button = QPushButton("Quit")

        credits_button.setSizePolicy(QSizePolicy.Fixed, QSizePolicy.Fixed)
        back_button.setSizePolicy(QSizePolicy.Fixed, QSizePolicy.Fixed)
        quit_button.setSizePolicy(QSizePolicy.Fixed, QSizePolicy.Fixed)

        back_button.clicked.connect(self._wrap_async(self.back))
        credits_button.clicked.connect(self._wrap_async(self.credits))
        quit_button.clicked.connect(self._wrap_async(self.quit))

        bottom_bar.addWidget(credits_button)
        bottom_bar.addStretch()
        bottom_bar.addWidget(back_button)
        bottom_bar.addStretch()
        bottom_bar.addWidget(quit_button)

        self.main_layout.addLayout(bottom_bar)

        self.setLayout(self.main_layout)

    def _wrap_async(self, coro_func):
        def wrapper():
            asyncio.create_task(coro_func())

        return wrapper

    async def update_username(self, new_username):
        self.username = new_username
        self.username_label.setText(f"User: {new_username}")

    async def update_game_frames(self, games):
        # Clear existing game frames
        for i in reversed(range(self.game_list_layout.count())):
            item = self.game_list_layout.takeAt(i)
            widget = item.widget()
            if widget is not None:
                widget.deleteLater()

        # Add new games
        for game in games[:len(games) - 1]:
            game_frame = await self.create_game_frame(game)
            self.game_list_layout.addWidget(game_frame)

    async def create_game_frame(self, game_name):
        frame = QFrame()
        frame.setFrameShape(QFrame.Box)
        frame.setStyleSheet("padding: 10px;")

        layout = QHBoxLayout()
        layout.setContentsMargins(10, 5, 10, 5)

        title = QLabel(game_name)
        title.setAlignment(Qt.AlignLeft | Qt.AlignVCenter)

        play_button = QPushButton("Play")
        info_button = QPushButton("Info")

        # Connect the play button to the function, passing the game name
        play_button.clicked.connect(self._wrap_async(partial(self.start_game, game_name)))
        info_button.clicked.connect(self._wrap_async(partial(self.info_game, game_name)))

        layout.addWidget(title)
        layout.addStretch()
        layout.addWidget(play_button)
        layout.addWidget(info_button)

        frame.setLayout(layout)
        return frame

    async def start_game(self, game_name=None):
        global game_to_play
        if game_name is None or game_name is False:
            print("-START NONE MODE-")
            game_name = game_to_play
        #print(f"Starting game: {game_name}")
        game_info_win.hide()
        await async_send_message(self.writer, f"GAME|{game_name}")

    async def info_game(self, game_name):
        global game_to_play
        game_to_play = game_name
        print(f"-REQUESTING GAME INFO: {game_name}-")
        await async_send_message(self.writer, f"INFO|{game_name}")

    async def credits(self):
        self.hide()
        credits_win.prev_window = "Game_sel"
        credits_win.show()

    async def back(self):
        self.hide()
        await async_send_message(self.writer, "BACK|MEN")
        global window
        # disable connect button and server pin on window
        window.cover_image.setPixmap(QPixmap(f"clientDATA/Main Page cover/{cover_arts[random.randrange(0, len(cover_arts))]}"))
        window.show()

    async def quit(self):
        if self.writer is not None:
            await async_send_message(self.writer,  "QUITGAME")
        QApplication.instance().quit()


class CreditsWindow(QWidget):
    def __init__(self, username, reader, writer):
        super().__init__()
        self.username = username
        self.reader = reader
        self.writer = writer
        self.prev_window = "Game_sel"
        self.setWindowTitle("Bio")
        self.setGeometry(700, 400, 800, 600)
        self.init_ui()

    def init_ui(self):
        main_layout = QVBoxLayout()
        main_layout.setSpacing(0)
        main_layout.setContentsMargins(0, 0, 0, 0)
        #self.setFixedSize(1920,1080)
        # Top Bar
        top_bar = QHBoxLayout()
        top_bar.setContentsMargins(10, 10, 10, 10)

        self.username_label = QLabel(f"User: {self.username}")
        self.username_label.setAlignment(Qt.AlignLeft | Qt.AlignVCenter)

        title_label = QLabel("Bio")
        title_label.setAlignment(Qt.AlignCenter)
        title_label.setStyleSheet("font-weight: bold; font-size: 20px;")

        # account_button = QPushButton("Account")
        # account_button.setSizePolicy(QSizePolicy.Fixed, QSizePolicy.Fixed)

        top_bar.addWidget(self.username_label)
        top_bar.addStretch()
        top_bar.addWidget(title_label)
        top_bar.addStretch()
        # top_bar.addWidget(account_button)

        main_layout.addLayout(top_bar)

        # Main Content: Bio Text
        credits_text = QTextBrowser()
        credits_text.setOpenExternalLinks(True)
        credits_text.setTextColor(QColor("white"))
        credits_text.setFontPointSize(16)
        html_links = """
        <div style="font-family: 'Segoe UI', sans-serif; font-size: 18px; color: #FFFFFF;">
            <div style="margin-bottom: 10px;"><h2>Sources:\n</h2></div>
            <div style="margin-bottom: 10px;"><a href="https://gbdev.io/pandocs/" style="color: #87CEFA; text-decoration: none;">Gameboy Pandocs</a></div>
            <div style="margin-bottom: 10px;"><a href="https://meganesu.github.io/generate-gb-opcodes/" style="color: #87CEFA; text-decoration: none;">Gameboy CPU/Sharp LR35902 Opcodes</a></div>
            <div style="margin-bottom: 10px;"><a href="https://wiki.libsdl.org/SDL2/APIByCategory" style="color: #87CEFA; text-decoration: none;">SDL2 Documentation</a></div>
            <div style="margin-bottom: 10px;"><a href="https://www.youtube.com/watch?v=e87qKixKFME&list=PLVxiWMqQvhg_yk4qy2cSC3457wZJga_e5" style="color: #87CEFA; text-decoration: none;">Building a Gameboy Emulator in C (YouTube)</a></div>
            <div style="margin-bottom: 10px;"><a href="https://en.wikipedia.org/wiki/CHIP-8" style="color: #87CEFA; text-decoration: none;">CHIP8 Wiki</a></div>
            <div style="margin-bottom: 10px;"><a href="https://www.youtube.com/watch?v=YvZ3LGaNiS0&list=PLT7NbkyNWaqbyBMzdySdqjnfUFxt8rnU_" style="color: #87CEFA; text-decoration: none;">Building a Chip8 Emulator in C (YouTube)</a></div>
            <div style="margin-bottom: 10px;"><a href="https://www.youtube.com/watch?v=06D1tBKeTB4" style="color: #87CEFA; text-decoration: none;">Setting up Make in VSCode</a></div>
            <div style="margin-bottom: 10px;"><a href="https://www.youtube.com/watch?v=H08t6gD1Y1E" style="color: #87CEFA; text-decoration: none;">Setting up SDL2 for Windows</a></div>
            <div style="margin-bottom: 10px;"><a href="https://chat.openai.com/" style="color: #87CEFA; text-decoration: none;">ChatGPT (for concept help)</a></div>
            <div style="margin-bottom: 10px;"><a href="https://www.geeksforgeeks.org/" style="color: #87CEFA; text-decoration: none;">GeeksForGeeks.org</a></div>
            <div style="margin-bottom: 10px;"><a href="https://www.w3schools.com/c/index.php" style="color: #87CEFA; text-decoration: none;">W3Schools - C</a></div>
            <div style="margin-bottom: 10px;"><a href="https://github.com/sysprog21/gameboy-emu" style="color: #87CEFA; text-decoration: none;">APU (used from this emulator)</a></div>
            <div style="margin-bottom: 10px;"><a href="https://github.com/rockytriton/LLD_gbemu/blob/main/README.md" style="color: #87CEFA; text-decoration: none;">Inspiration from this emulator</a></div>
            <div style="margin-bottom: 10px;"><a href="https://github.com/Jormit/Gameboy-Emulator" style="color: #87CEFA; text-decoration: none;">CPP Gameboy Emulator (function blueprint)</a></div>
            <div style="margin-bottom: 10px;"><a href="https://doc.qt.io/qtforpython-5/index.html" style="color: #87CEFA; text-decoration: none;">PyQt5 Documentation</a></div>
            <div style="margin-bottom: 10px;"><a href="https://tecadmin.net/install-openssl-on-windows/" style="color: #87CEFA; text-decoration: none;">Setting up OpenSSL on Windows</a></div>
            <div style="margin-bottom: 10px;"><a href="https://docs.python.org/3/library/asyncio.html" style="color: #87CEFA; text-decoration: none;">Asyncio Documentation</a></div>
        </div>
        """

        credits_text.setHtml(html_links)

        main_layout.addWidget(credits_text)

        # Bottom Bar
        bottom_bar = QHBoxLayout()
        bottom_bar.setContentsMargins(10, 10, 10, 10)

        credits_button = QPushButton("Bio")
        back_button = QPushButton("Back")
        quit_button = QPushButton("Quit")

        quit_button.clicked.connect(self._wrap_async(self.quit))
        back_button.clicked.connect(self._wrap_async(self.back))

        bottom_bar.addWidget(credits_button)
        bottom_bar.addStretch()
        bottom_bar.addWidget(back_button)
        bottom_bar.addStretch()
        bottom_bar.addWidget(quit_button)

        main_layout.addLayout(bottom_bar)

        self.setLayout(main_layout)

    def _wrap_async(self, coro_func):
        def wrapper():
            asyncio.create_task(coro_func())

        return wrapper

    async def back(self):
        self.hide()

        if self.prev_window == "menu":
            global window
            window.cover_image.setPixmap(QPixmap(f"clientDATA/Main Page cover/{cover_arts[random.randrange(0, len(cover_arts))]}"))
            window.show()
        elif self.prev_window == "Game_sel":
            global game_sel
            game_sel.show()
        elif self.prev_window == "info":
            global game_info_win
            game_info_win.show()

    async def quit(self):
        if self.writer is not None:
            await async_send_message(self.writer, "QUITGAME")
        QApplication.instance().quit()

    async def update_credits_text(self, credits_content):
        credits_text = self.findChild(QTextEdit)
        credits_text.setText(credits_content)

    async def update_username(self, new_username):
        self.username = new_username
        self.username_label.setText(f"User: {new_username}")

class GameInfoWindow(QWidget):
    def __init__(self, username, writer, reader):
        super().__init__()
        self.username = username
        self.writer = writer
        self.reader = reader
        self.setWindowTitle("Game Info")
        self.setGeometry(700, 400, 800, 600)
        self.init_ui()

    def init_ui(self):
        main_layout = QVBoxLayout()
        #main_layout.setSpacing(0)
        #main_layout.setContentsMargins(10, 10, 10, 10)
        #self.setFixedSize(800,600)
        # Top Bar
        top_bar = QHBoxLayout()
        #top_bar.setContentsMargins(10, 10, 10, 10)

        self.username_label = QLabel(f"User: {self.username}")
        self.username_label.setAlignment(Qt.AlignLeft | Qt.AlignVCenter)

        title_label = QLabel("GAME INFO")
        title_label.setAlignment(Qt.AlignCenter)
        title_label.setStyleSheet("font-weight: bold; font-size: 20px;")

        #account_button = QPushButton("Account")
        #account_button.setSizePolicy(QSizePolicy.Fixed, QSizePolicy.Fixed)

        top_bar.addWidget(self.username_label)
        top_bar.addStretch()
        top_bar.addWidget(title_label)
        top_bar.addStretch()
        #top_bar.addWidget(account_button)

        main_layout.addLayout(top_bar)

        # Main Content (Poster + Info)
        content_layout = QHBoxLayout()
        content_layout.setContentsMargins(20, 20, 20, 20)
        content_layout.setSpacing(20)

        # Left Column: Poster + Play Button
        left_column = QVBoxLayout()
        left_column.setSpacing(10)
        left_column.setAlignment(Qt.AlignTop)

        self.poster_label = QLabel()
        self.poster_label.setPixmap(QPixmap())  # Placeholder for poster
        self.poster_label.setFixedSize(240, 320)
        self.poster_label.setStyleSheet("border: 1px solid gray;")

        self.play_button = QPushButton("Play Now")
        self.play_button.setFixedWidth(240)
        self.play_button.clicked.connect(self._wrap_async(game_sel.start_game))

        left_column.addWidget(self.poster_label)
        left_column.addWidget(self.play_button)

        # Info Panel (Right)
        info_panel = QVBoxLayout()
        info_panel.setSpacing(10)
        info_panel.setAlignment(Qt.AlignTop)

        self.game_title_label = QLabel("Game Title")
        self.game_title_label.setStyleSheet("font-weight: bold; font-size: 18px;")

        self.release_date_label = QLabel("Release Date: TBD")
        self.description_text = QLabel("Game was made in ....")

        info_panel.addWidget(self.game_title_label)
        info_panel.addWidget(self.release_date_label)
        info_panel.addWidget(self.description_text)

        content_layout.addLayout(left_column)
        content_layout.addLayout(info_panel)

        main_layout.addLayout(content_layout)

        # Bottom Bar
        bottom_bar = QHBoxLayout()
        bottom_bar.setContentsMargins(10, 10, 10, 10)

        credits_button = QPushButton("Bio")
        back_button = QPushButton("Back")
        quit_button = QPushButton("Quit")

        # Bottom Bar
        bottom_bar = QHBoxLayout()
        bottom_bar.setContentsMargins(10, 10, 10, 10)

        credits_button = QPushButton("Bio")
        back_button = QPushButton("Back")
        quit_button = QPushButton("Quit")

        back_button.clicked.connect(self._wrap_async(self.back))
        credits_button.clicked.connect(self._wrap_async(self.credits))
        quit_button.clicked.connect(self._wrap_async(self.quit))

        bottom_bar.addWidget(credits_button)
        bottom_bar.addStretch()
        bottom_bar.addWidget(back_button)
        bottom_bar.addStretch()
        bottom_bar.addWidget(quit_button)

        main_layout.addLayout(bottom_bar)

        self.setLayout(main_layout)


    def _wrap_async(self, coro_func):
        def wrapper():
            asyncio.create_task(coro_func())

        return wrapper

    async def update_username(self, new_username):
        self.username = new_username
        self.username_label.setText(f"User: {new_username}")

    async def load_image_from_bytes(self, byte_data):
        print("loading")
        image = QImage()
        image.loadFromData(byte_data)
        return QPixmap.fromImage(image)

    async def update_game_info(self, title, release_date, description, image_data):
        print("-UPDATING INFO-")

        try:
            self.game_title_label.setText(title.split('.')[0])
            self.release_date_label.setText(f"Release Date: {release_date}")
            self.description_text.setText(description)

            print("cleaned")
            pixmap_raw = await self.load_image_from_bytes(ast.literal_eval(image_data))
            game_info_win.poster_label.setPixmap(pixmap_raw)

            self.show()
        except Exception as e:
            print(f"*error while updating info: {e}*")
    async def credits(self):
        self.hide()
        credits_win.prev_window = "info"
        credits_win.show()

    async def back(self):
        self.hide()
        game_sel.show()

    async def quit(self):
        if self.writer is not None:
            await async_send_message(self.writer, "QUITGAME")
        QApplication.instance().quit()


#endregion

#region DATA ACQUISITION

async def update_greeting(username):
    await game_sel.update_username(username)
    await credits_win.update_username(username)
    await game_info_win.update_username(username)

    window.hide()

    game_sel.show()
    # game_sel.showFullScreen()

    #max_emit("sel")
    #communicate.show_max.emit("sel")

async def update_game_list(writer):
    await async_send_message(writer=writer, message="MAN")

#endregion

#region Mainloop
async def start_client():
    global app
    global app_stylesheet
    global discon
    global HOST, PORT
    global LOCAL_CERT
    try:

        window.con_button.setStyleSheet("background-color: yellow; color:black;")
        window.con_button.setEnabled(False)

        conn_args = {
            'host': HOST,
            'port': PORT,
        }

        if LOCAL_CERT:
            conn_args['ssl'] = ssl_context

        reader, writer = await asyncio.wait_for(asyncio.open_connection(
            **conn_args
        ), timeout=5)


        print("***SSL Handshake successful***")
        discon = False

        asyncio.create_task(receive_messages(reader, writer))

        print("[CONNECTED] Connected to the server.")
    except asyncio.TimeoutError:
        window.con_button.setStyleSheet("background-color: red;")
        window.con_button.setEnabled(False)
        print("Connection timed out")
        await asyncio.sleep(2)
        window.con_button.setStyleSheet(app_stylesheet)
        window.con_button.setEnabled(True)
    except ConnectionRefusedError:
        window.con_button.setStyleSheet("background-color: red;")
        window.con_button.setEnabled(False)
        print(f"*Encountered ERROR while attempting to connect to server*")
        print("\tConnection refused: Server may be overloaded or blocking your IP.")
        await asyncio.sleep(2)
        window.con_button.setStyleSheet(app_stylesheet)
        window.con_button.setEnabled(True)
    except ssl.SSLError as e:
        window.con_button.setStyleSheet("background-color: red;")
        window.con_button.setEnabled(False)
        print(f"*Encountered ERROR while attempting to connect to server*")
        print(f"\tSSL Error: {e}")
        await asyncio.sleep(2)
        window.con_button.setStyleSheet(app_stylesheet)
        window.con_button.setEnabled(True)
    except (ConnectionResetError, BrokenPipeError):
        window.con_button.setStyleSheet("background-color: red;")
        window.con_button.setEnabled(False)
        print(f"*Encountered ERROR while attempting to connect to server*")
        print("\tServer closed the connection unexpectedly.")
        await asyncio.sleep(2)
        window.con_button.setStyleSheet(app_stylesheet)
        window.con_button.setEnabled(True)
    except WindowsError:
        window.con_button.setStyleSheet("background-color: red;")
        window.con_button.setEnabled(False)
        print(f"*Encountered ERROR while attempting to connect to server*")
        print("\tEncountered Windows error")
        await asyncio.sleep(2)
        window.con_button.setStyleSheet(app_stylesheet)
        window.con_button.setEnabled(True)
    else:
        window.con_button.setStyleSheet("background-color: green;")
        window.pin_line_edit.setEnabled(False)
        window.con_button.setEnabled(False)

        window.username_line_edit.setEnabled(True)
        window.password_line_edit.setEnabled(True)
        window.register_button.setEnabled(True)
        window.login_button.setEnabled(True)
        window.credits_btn.setEnabled(True)

        window.reader, window.writer = reader, writer
        game_sel.reader, game_sel.writer = reader, writer
        credits_win.reader, credits_win.writer = reader, writer
        game_info_win.reader, game_info_win.writer = reader, writer

        #app.aboutToQuit.connect(cleanup)

async def receive_messages(reader, writer):
    global game_loop
    while True:
        try:
            raw_msglen = await async_recvall(reader, 4)
            if not raw_msglen:
                break
            msglen = struct.unpack('>I', raw_msglen)[0]
            # print(f"MESSAGE LENGTH: {msglen}")
            message = await async_recvall(reader, msglen)
            message = message.decode()

            if message:
                print(f"[SERVER MESSAGE] [LEN]:{len(message)} = {message[0:100]}")
                if message.startswith("User created Successfully") or message.startswith("User Logged in Successfully"):
                    await update_greeting(message.split('|')[1])
                    await update_game_list(writer)

                if message.startswith("Online game list"):
                    manifest = message.split('|')[1].split('!')
                    await game_sel.update_game_frames(manifest)

                if message.startswith("BAN"):
                    writer.close()
                    await writer.wait_closed()
                    print("[Getting Banned]")
                    break

                if message.startswith("INFO"):
                    print("HOI")
                    info_to_load = message.split("||")
                    game_sel.hide()
                    global game_to_play
                    game_to_play = f"{info_to_load[1].split('.')[0]}.gb"

                    await game_info_win.update_game_info(info_to_load[1], info_to_load[3], info_to_load[2], info_to_load[4])

                if message.startswith("GAMEDATA"):
                    game_file_data = message[9:]
                    print("-ENTERING GAME MODE-")

                    game_sel.hide()
                    # hide game info screen as well when launching game
                    global p
                    p = await asyncio.create_subprocess_exec(
                        emulator_path,
                        stdin=asyncio.subprocess.PIPE,
                        text=False
                    )

                    try:
                        game_loop = True

                        in_game_task = asyncio.create_task(in_game(game_file_data, p))

                        #print("[GAME PROCESS INITIATED]")
                        while game_loop is True:
                            print("waiting")
                            await async_send_message(writer, "check")
                            await asyncio.sleep(5)
                        #await in_game(game_file_data, p)

                        in_game_task.cancel()

                        print("-Game Loop completed-")
                        #p.kill()
                        await p.wait()
                        #
                        await asyncio.wait_for(p.wait(), timeout=1)
                        print("*TERMINATING GAME PROCESS*")

                        p = None

                    except (Exception, asyncio.TimeoutError):

                        p.kill()
                        game_loop = False
                        print("**ERROR WHILE RUNNING EMU**")
                        writer.close()
                        await writer.wait_closed()
                        await discon_reset()



            else:
                print("BREAKING")
                break

        except:
            break


async def in_game(game_data, process):
    global game_loop
    await process.communicate(input=ast.literal_eval(game_data))  # BLOCK
    game_loop = False
    print(f"-GAME HAS ENDED: {not game_loop}-")
    if not discon:
        game_sel.show()
    return

#endregion

#region Reset and loose end functions

async def discon_reset():
    global discon
    global app_stylesheet
    global cover_arts
    discon = True
    game_sel.hide()
    window.con_button.setStyleSheet(app_stylesheet)
    window.pin_line_edit.setEnabled(True)
    window.con_button.setEnabled(True)

    window.username_line_edit.setEnabled(False)
    window.password_line_edit.setEnabled(False)
    window.register_button.setEnabled(False)
    window.login_button.setEnabled(False)
    window.credits_btn.setEnabled(True)

    window.cover_image.setPixmap(QPixmap(f"clientDATA/Main Page cover/{cover_arts[random.randrange(0, len(cover_arts))]}"))
    window.show()


#endregion


async def main():

    global app
    global app_stylesheet
    app = QApplication(sys.argv)
    loop = QEventLoop(app)
    asyncio.set_event_loop(loop)

    # Apply stylesheet
    app_stylesheet = """
        QWidget {
            background-color: #2E2E2E;
            color: #FFFFFF;
            font-family: 'Comic Sans MS';
            font-size: 18px;
        }
        QLineEdit {
            background-color: #2E2E2E;
            border: 1px solid #5A5A5A;
            border-radius: 4px;
            padding: 8px;
            color: #FFFFFF;
        }
        QLabel {
            padding: 4px;
        }
        QPushButton {
            background-color: #5A9;
            border: none;
            border-radius: 4px;
            padding: 10px;
            color: #FFFFFF;
            font-weight: bold;
        }

        QPushButton:hover { background-color: #48D; }
        QPushButton:pressed { background-color: #367; }
        QLabel#titlelabel {
            font-size: 48px;
            font-family: Comic-Sans-MS;
            font-weight: bold;
        }
        QTextEdit {
            font-size: 14px;
            color: #FFFFFF;
            font-weight: bold;
            
        }
    """
    app.setStyleSheet(app_stylesheet)

    writer = None
    reader = None
    global window, game_sel, credits_win, game_info_win
    # Instantiate your windows
    logo_dir = "clientDATA/logo.ico"
    window = MenuWindow(writer, reader)
    window.setWindowTitle("RetroCloudClient")
    window.setWindowIcon(QIcon(logo_dir))
    window.cover_image.setPixmap(QPixmap(f"clientDATA/Main Page cover/{cover_arts[random.randrange(0, len(cover_arts))]}"))
    window.show()


    game_sel = GameSelectWindow("TBD", writer, reader)
    game_sel.setWindowTitle("RetroCloudClient")
    game_sel.setWindowIcon(QIcon(logo_dir))
    game_sel.hide()

    credits_win = CreditsWindow("TBD", writer, reader)
    credits_win.setWindowTitle("RetroCloudClient")
    credits_win.setWindowIcon(QIcon(logo_dir))
    credits_win.hide()

    game_info_win = GameInfoWindow("TBD", writer, reader)
    game_info_win.setWindowTitle("RetroCloudClient")
    game_info_win.setWindowIcon(QIcon(logo_dir))
    game_info_win.hide()


    with loop:
        loop.run_forever()



if __name__ == "__main__":
    asyncio.run(main(), debug=True)
