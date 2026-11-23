"""Scripted FICS guest used to test the 3DS client against a real server (no Lichess account involved).
usage: bot.py LOGFILE SEEK_ARGS MOVES_WHITE MOVES_BLACK [END] [MAXSEC]
  SEEK_ARGS   e.g. "4 3 u"      (posted with 'seek')   or  match:<name>
  MOVES_*     comma separated moves played in order when it is the bot's turn (as white / as black)
  END         resign | draw | none   what to do after the moves are used up
"""
import socket,sys,time,re,os
DELAY=float(os.environ.get('BOT_DELAY','0'))
log=open(sys.argv[1],'w',buffering=1)
seekargs=sys.argv[2]; mw=sys.argv[3].split(','); mb=sys.argv[4].split(',')
end=sys.argv[5] if len(sys.argv)>5 else 'resign'; maxsec=int(sys.argv[6]) if len(sys.argv)>6 else 120
s=socket.create_connection(("freechess.org",5000),timeout=20); s.settimeout(0.5)
def send(c): log.write(">> "+c+"\n"); s.sendall((c+"\r\n").encode())
buf=''; t0=time.time(); state='login'; nmoves=0; myname=None; started=False; done=False
def lines():
    global buf
    try: d=s.recv(4096).decode('latin1')
    except socket.timeout: d=''
    except Exception: d=''
    buf+=d.replace('\r','')
    while '\n' in buf:
        l,buf=buf.split('\n',1); yield l
    if buf.endswith('login: ') and state=='login': yield buf
    if 'Press return' in buf: yield buf
while time.time()-t0<maxsec and not done:
    for l in lines():
        l=l.replace('fics% ','')
        if not l.strip(): continue
        log.write("<< "+l+"\n")
        if l.startswith('login:') and state=='login': send('guest'); state='pw'
        elif 'Press return to enter' in l and state=='pw':
            m=re.search(r'"(Guest\w+)"',l); myname=m.group(1) if m else None
            send(''); state='in'
            for c in ["set style 12","iset ms 1","iset gameinfo 1","set shout 0","set cshout 0","set seek 0","set pin 0","set gin 0","set bell 0","set open 1","set silence 1"]: send(c)
            if seekargs.startswith('match:'): pass
            else: send("seek "+seekargs)
        elif l.startswith('<12>'):
            f=l.split()
            rel=int(f[19]) if len(f)>19 else 0
            started=True
            if rel==1:
                time.sleep(DELAY)
                color='w' if f[9]=='W' else 'b'
                mv=(mw if color=='w' else mb)
                if nmoves<len(mv): send(mv[nmoves]); nmoves+=1
                elif end=='resign': send('resign')
                elif end=='draw': send('draw')
        elif 'Challenge:' in l or 'would like to take back' in l or 'offers you a draw' in l: 
            if 'offers you a draw' in l: send('accept')
            if 'Challenge:' in l and seekargs.startswith('match:'): send('accept')
        elif re.match(r'\{Game \d+ .*\} (1-0|0-1|1/2-1/2|\*)',l): time.sleep(1); done=True
send('quit'); time.sleep(.3)
