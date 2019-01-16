killall lsyncd
./hub 192.168.159.148 &> hub.log | /usr/local/websocketd  --port 1234 --devconsole tail -f ./hub.log
