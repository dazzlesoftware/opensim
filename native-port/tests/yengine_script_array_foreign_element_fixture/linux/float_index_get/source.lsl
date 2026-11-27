YOption allowall;
object foreign; integer[] input; integer stage; integer result; integer after; integer rhs; string kind;
integer Right(){rhs++;return 2;}
default{state_entry(){llSleep(0.1);input=(integer[])foreign;stage=1;kind=xmrTypeName((object)input);result=input[0];after=1;}}
