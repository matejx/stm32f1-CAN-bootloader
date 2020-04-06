#!/usr/bin/python3

import sys,socket,struct,datetime,crcmod

UDP_IP = '192.168.4.102'
UDP_TX_PORT = 1234
UDP_RX_PORT = 50006
PG_SIZE = 1024
BL_WBUF = 1
BL_WPAGE = 2
BL_WCRC = 3

def canmsg(id, data):
	if len(id) > 3:
		raise ValueError("invalid id")
	if len(data) > 8:
		raise ValueError("invalid data")
	d = bytearray(36) # initializes elements to zero
	d[1] = 0x24 # = 36
	d[3] = 0x80
	d[21] = len(data)
	for i in range(len(id)):
		d[28-len(id)+i] = id[i]
	for i in range(len(data)):
		d[28+i] = data[i]
	return d

def bl_cmd(board_id, cmd, par1, par2):
	d = bytearray(8)
	d[0] = board_id
	d[1] = cmd
	d[2] = par1 & 0xff
	d[3] = par1 >> 8
	for i in range(4):
	  d[4+i] = par2[3-i]
	txsock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
	txsock.settimeout(1)
	txsock.sendto(canmsg(b'\xb0',d), (UDP_IP, UDP_TX_PORT))

def bl_waitresp(rxsock, board_id, bl_cmd, to_sec):
	absto = datetime.datetime.now() + datetime.timedelta(seconds=to_sec)
	while datetime.datetime.now() < absto:
		udp_data, addr = rxsock.recvfrom(64)
		can_dlc = udp_data[21]
		can_id = struct.unpack('!I', udp_data[24:28])[0]
		can_data = udp_data[28:36]
		if (can_id == 0xb1) and (can_dlc >= 3) and (can_data[0] == board_id) and (can_data[1] == bl_cmd):
			return can_data[2]

# -----------------------------------------------------------------------------

if __name__ == "__main__":
	if len(sys.argv) < 3:
		print("usage: prg.py board_id file_name")
		sys.exit(0)

	board_id = int(sys.argv[1])

	f = open(sys.argv[2],"rb")
	b = bytearray(f.read())
	f.close()
	b.extend(bytearray(PG_SIZE - (len(b) % PG_SIZE)))

	rxsock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
	rxsock.settimeout(1)
	rxsock.bind(('0.0.0.0', UDP_RX_PORT))

	acrc = crcmod.Crc(0x104c11db7, initCrc=0xffffffff, rev=False)

	for p in range(len(b)//PG_SIZE):
		pcrc = crcmod.Crc(0x104c11db7, initCrc=0xffffffff, rev=False)
		for w in range(PG_SIZE//4):
			a = (p*PG_SIZE) + (w*4)
			d = b[a:a+4][::-1] # take out 4 bytes and reverse them
			acrc.update(d)
			pcrc.update(d)
			bl_cmd(board_id, BL_WBUF, w, d)
			r = bl_waitresp(rxsock, board_id, BL_WBUF, 3)
			if r > 0:
				raise RuntimeError('bl wbuf error ' + str(r))
		bl_cmd(board_id, BL_WPAGE, p, pcrc.digest())
		r = bl_waitresp(rxsock, board_id, BL_WPAGE, 3)
		if r > 0:
			raise RuntimeError('bl wpage error ' + str(r))
		else:
			print("page " + str(p) + " OK")

	bl_cmd(board_id, BL_WCRC, p+1, acrc.digest())
	r = bl_waitresp(rxsock, board_id, BL_WCRC, 3)
	if r > 0:
		raise RuntimeError('bl wcrc error ' + str(r))

	print("all OK")
