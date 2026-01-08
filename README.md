# simp
Stylus Imput Mapper


sudo apt update
sudo apt install -y g++ libx11-dev libxi-dev libxtst-dev libjansson-dev

g++ -O2 -std=c++17 touchwm_daemon_hotconfig.cpp -o touchwm_daemon_hotconfig \
  -lX11 -lXi -lXtst -ljansson





  ./touchwm_daemon_hotconfig ~/.config/touchwm/default.conf