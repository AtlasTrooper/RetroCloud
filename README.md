# RetroCloud 
<img width="160" height="160" alt="logo" src="https://github.com/user-attachments/assets/532d8b2c-5289-41e1-a1bf-b4f813c3a90e" />

Retro cloud, is a client-server based application, that allows the user to quickly and conveniently play their favorite retro games, without having to go through much setup, that being handled by the server. The server host organizes a ROM library, and the clients simply connect and stream the game ROM data from the server.

*Note*: This is a project of mine made in highschool, it is by no means proffessional grade, and I would recommend reviewing and most likely modifying the code in the event that it might be used in a production environment of any kind. What you might find here may be innefficient or not very well written. Additionally, I unfortunately cannot provide the game ROMS I used for quite obvious reasons.

# Setup 
  # LAN:
  1. Download OpenSSL:  https://slproweb.com/products/Win32OpenSSL.html 
  2. Setup OpenSSL on your device: https://tecadmin.net/install-openssl-on-windows/
  3. Setup some form of Certificate that is compatible with OpenSSL and route the paths in both client and server to the certificate files(example. if the certificates are in the project directory, make an Encryption folder).
     Later in the README you will find a guide on setting them up the way I did, but it should work with other certificates too.
  4. Make sure the LOCAL_CERT variable is set to true for both client and server, otherwise the client and server will not use ssl when running and there will be no encryption layer.

  # WAN/Portfowarding: 
  In this case, it is assumed that the server and or tunnel service that is being used to put the server on the open internet, already provides the encryption/security layer
  
  1. Make sure the LOCAL_CERT variable is set to false for both client and server.
  2. Make sure the client can accept a server ip of the type you are using, since currently it is configured to be able to use a normal ipv4 or an ngrok tcp endpoint(if you use something similar to ngrok where the forwarding address
     is not like a standard ip address,  you might need to modify the pin_connect function i the MenuWindow class).
  
  # Server Setup:
  <img width="254" height="121" alt="image" src="https://github.com/user-attachments/assets/23740c10-a34c-4822-8979-84adf790e856" />

  1. Acquire ROMS(currently the emulator is compatible with original gameboy roms up to MBC1, maybe one day I'll add MBC1+ROM+BATTERY for games like Link's Awakening 
  2. Make a roms folder in ServerDATA/ and place the ROM files in there
  3. You can add optional game descriptive text and cover art in the serverDATA/info_pages/ Art and Text folders
  4. Set the Server IP and PORT
  5. Run the asyncServer.py file
  
  # Client Setup:
  1. Make sure the main_menu.ui file is in the UI folder or in a path accessible to the client file
  2. You can add optional cover arts in the clientDATA/Main Page cover folder, these are cycled through at a random order whenever you return to menu
  3. Build the emulator and make sure the resulting executable is exposed to the emulator_path variable. The most up to date version of the emulator (and a built exe) is provided with the project files, but
     should you wish to rebuild it or make changes, then see "Building the Emulator" below.
  5. Run the asyncClient.py file

  # Building the Emulator(only needed for clients):
  1. Download the OneFileEmu folder and open it up in an editor(I used Vscode)
  2. For Local play(running the exe from a terminal and providing the ROM via a path) set the ALT_CART variable to 0
  3. For use with the rest of this project(game streaming) set the ALT_CART variable to 1
  4. Run the make file and build the exe.

# Self Signed certificate setup:
1. Unzip the Crt Generation files New 3.13 1 zip file
2. Input your details in the rootCA.conf file and the serverCSR.conf file
3. Add the server ip to the server.conf file
4. Run the Crt_Gen file
5. (Optional) Add the rootCA.crt to your computer's accepted certificates(very easy via google search)
6. Make sure rootCa.crt's path is exposed to the client file under the CERTIFICATE_FILE variable
7. Make sure the server.crt and server.key files are exposed to the asyncServer.py file
   <img width="565" height="54" alt="image" src="https://github.com/user-attachments/assets/ce2bca30-a4da-4feb-9a83-d4196883ab52" />


# Credits: 
While writing the backend and making the graphics wasn't too much of a hassle, coding the emulator was probably the hardest part of the project. 
The final emulator provided in the repo is my implementation of the Gameboy-Emulator project made by github user Jordan Mitchell https://github.com/Jormit/Gameboy-Emulator . 
I really liked the structure he used and I relied heavily on it, basing my emulator on much of his original code. I followed his emulator as a blueprint, essentially using the same functions and some variables 
but writing their implementation myself. In some places where no changes were really necessary, I kept his code in place. 
I wrote my implementation fully in C, and changed the way ROMS were loaded into the emulator in order to support the ROM "streaming" from the backend. 
I also made many more changes where I saw they were fit to add(such as removing redundant functions and replacing them with more streamlined and simple alternatives)
not to mention splitting some of the code into several files, increasing the overall resolution of the graphcis, etc...

The APU(Audio processing unit) emulator used for this project is from the gameboy-emu project made by github user sysprog21: https://github.com/sysprog21/gameboy-emu . 
The APU emulation was really well done, it was very easy to setup, feeling almost like a plug and play solution.

Additional sources:
<div style="margin-bottom: 10px;"><a href="https://gbdev.io/pandocs/" style="color: #87CEFA; text-decoration: none;">Gameboy Pandocs</a>
            <div style="margin-bottom: 10px;"><a href="https://meganesu.github.io/generate-gb-opcodes/" style="color: #87CEFA; text-decoration: none;">Gameboy CPU/Sharp LR35902 Opcodes</a>
            <div style="margin-bottom: 10px;"><a href="https://wiki.libsdl.org/SDL2/APIByCategory" style="color: #87CEFA; text-decoration: none;">SDL2 Documentation</a>
            <div style="margin-bottom: 10px;"><a href="https://www.youtube.com/watch?v=e87qKixKFME&list=PLVxiWMqQvhg_yk4qy2cSC3457wZJga_e5" style="color: #87CEFA; text-decoration: none;">Building a Gameboy Emulator in C (YouTube), great tutorial series</a>
            <div style="margin-bottom: 10px;"><a href="https://www.youtube.com/watch?v=06D1tBKeTB4" style="color: #87CEFA; text-decoration: none;">Setting up Make in VSCode</a>
            <div style="margin-bottom: 10px;"><a href="https://www.youtube.com/watch?v=H08t6gD1Y1E" style="color: #87CEFA; text-decoration: none;">Setting up SDL2 for Windows</a>
            <div style="margin-bottom: 10px;"><a href="https://www.geeksforgeeks.org/" style="color: #87CEFA; text-decoration: none;">GeeksForGeeks.org</a></div>
            <div style="margin-bottom: 10px;"><a href="https://www.w3schools.com/c/index.php" style="color: #87CEFA; text-decoration: none;">W3Schools - C</a></div>
            <div style="margin-bottom: 10px;"><a href="https://doc.qt.io/qtforpython-5/index.html" style="color: #87CEFA; text-decoration: none;">PyQt5 Documentation</a>
            <div style="margin-bottom: 10px;"><a href="https://tecadmin.net/install-openssl-on-windows/" style="color: #87CEFA; text-decoration: none;">Setting up OpenSSL on Windows</a>
             <a href="https://docs.python.org/3/library/asyncio.html" style="color: #87CEFA; text-decoration: none;">Asyncio Documentation</a>

#

I really enjoyed making this project, mainly cause I really wanted an excuse to learn about and code an emulator,
and this concept of "game streaming" was my way of finding a project idea involving emulation, that would fit into the school's rubric. 

Enjoy!

*PS:* This project was made to work with the emulator provided in the repo, however, the python backend and client file,
are essentially just a wrapper for the emulator itself and can be easily modified to work with additional emulators.
Just make sure that any added emulators are capable of loading ROM files through stdin.
