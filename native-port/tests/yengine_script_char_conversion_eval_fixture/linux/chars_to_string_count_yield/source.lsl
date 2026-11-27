YOption allowall;
char[] input; string text; integer order; integer c1; integer c2; integer c3; integer c4; integer c5; integer after;
object Source(){c1++;order=order*10+1;return input;}
integer Start(){c2++;order=order*10+2;return 0;}
integer Count(){c3++;order=order*10+3;llSleep(0.1);return 1;}
default{state_entry(){input=new char[](1);input[0]=(char)65;text=xmrChars2String(Source(),Start(),Count());after=1;}}
