# @TEST-EXEC: btest-bg-run bro bro -b --pseudo-realtime -r $TRACES/socks.trace %INPUT
# @TEST-EXEC: btest-bg-wait -k 5
# @TEST-EXEC: btest-diff out

@TEST-START-FILE input.log
#separator \x09
#path	ssh
#fields	i	b	
#types	int	bool
1	T
2	T
3	F
4	F
5	F
6	F
7	T
@TEST-END-FILE

global outfile: file;

module A;

type Val: record {
	i: int;
	b: bool;
};

event line(description: Input::EventDescription, tpe: Input::Event, i: int, b: bool)
	{
	print outfile, description;
	print outfile, tpe;
	print outfile, i;
	print outfile, b;
	}

event bro_init()
	{
	outfile = open("../out");
	Input::add_event([$source="../input.log", $name="input", $fields=Val, $ev=line, $want_record=F]);
	Input::remove("input");
	}

event Input::end_of_data(name: string, source:string)
	{
	print outfile, "End-of-data";
	close(outfile);
	terminate();
	}
