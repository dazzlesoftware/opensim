YOption allowall;
char[] input; string text; integer order; integer c1; integer c2; integer c3; integer c4; integer c5; integer after;
string Source(){c1++;order=order*10+1;return "A";}
integer Start(){c2++;order=order*10+2;return 0;}
object Destination(){c3++;order=order*10+3;return input;}
integer Offset(){c4++;order=order*10+4;return 0;}
integer Count(){c5++;order=order*10+5;return 1;}
default{state_entry(){input=new char[](1);xmrString2Chars(Source(),Start(),Destination(),Offset(),Count());text=xmrChars2String(input,0,1);llSleep(0.1);after=1;}}
