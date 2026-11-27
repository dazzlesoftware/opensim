YOption allowall;
integer[] destination; object nil; integer after;
default {state_entry(){destination=new integer[](1);llSleep(0.1);xmrArrayCopy(nil,0,destination,0,1);after=1;}}
