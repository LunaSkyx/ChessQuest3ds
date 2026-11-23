import socket,time,sys
def rd(s,t=1.5):
    s.settimeout(t); out=b''
    try:
        while True:
            d=s.recv(4096)
            if not d: break
            out+=d
    except Exception: pass
    return out.decode('latin1')
s=socket.create_connection(("freechess.org",5000),timeout=15)
b=rd(s,2.5); print(repr(b[-120:]))
s.sendall(b"guest\r\n"); b=rd(s,2.5); print(repr(b[-300:]))
s.sendall(b"\r\n"); b=rd(s,3); print(repr(b[-500:]))
for c in ["set style 12","iset ms 1","iset gameinfo 1","set shout 0","set cshout 0","set seek 0","set pin 0","set gin 0","set bell 0","set interface Lichess3DS","variables","help seek"]:
    s.sendall((c+"\r\n").encode()); time.sleep(.4)
b=rd(s,2.5); print(b[:3500])
s.sendall(b"quit\r\n"); s.close()
