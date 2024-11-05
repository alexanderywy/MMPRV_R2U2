import os
import socket

# 获取本机用户名
username = os.getlogin()

# 获取主机名
hostname = socket.gethostname()

# 获取本机IP地址
ip_address = socket.gethostbyname(hostname)

print("用户名:", username)
print("主机名:", hostname)
print("IP地址:", ip_address)
