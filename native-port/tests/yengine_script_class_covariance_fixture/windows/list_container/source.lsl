YOption allowall;
integer[] input; object wrapped; object spare; integer after;
default { state_entry() { input=new integer[](3); input[0]=31; wrapped=input; llSleep(0.1); after=input[0]; } }
