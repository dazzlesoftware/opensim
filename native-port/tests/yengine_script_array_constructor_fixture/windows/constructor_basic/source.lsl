YOption allowall;
integer[] input; object pressure; integer result; integer before; integer during; integer after;
integer Use(integer[] value,integer ignored){input=value;return value.Length;}
integer Pause(){during=xmrHeapUsed();llSleep(0.1);return 7;}
default{state_entry(){before=xmrHeapUsed();result=Use(new integer[](4),Pause());after=1;}}
