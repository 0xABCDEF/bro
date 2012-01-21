##! Software detection with the FTP protocol.

# TODO:
#
# * Detect server software with initial 220 message
# * Detect client software with password given for anonymous users
#   (e.g. cyberduck@example.net)

@load base/frameworks/software

module FTP;

export {
	redef enum Software::Type += {
		## Identifier for FTP clients in the software framework.
		CLIENT,
		## Not currently implemented.
		SERVER,
	};
}

event ftp_request(c: connection, command: string, arg: string) &priority=4
	{
	if ( command == "CLNT" )
		{
		local si = Software::parse(arg, c$id$orig_h, CLIENT);
		Software::found(c$id, si);
		}
	}
