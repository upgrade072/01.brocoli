rm  -f ./hub.log
killall lsyncd
killall websocketd
./broker 192.168.159.100 &
./ahif 7000 &
./feaib 7100 &
./feaib 7100 192.168.159.148 &
./psib 7200 &
./regib 7300 &
./smsib 7400 &
./hub 192.168.159.100 &> hub.log &
/usr/local/websocketd --port 1234 --devconsole tail -f ./hub.log &
