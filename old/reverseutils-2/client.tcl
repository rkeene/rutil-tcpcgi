#!/usr/bin/tcl



proc dehexcode {str} {
  regsub -all {\+} $str " " str
  while {[set end [string first "%" $str]]!=-1} {
    set chval 0x[string range $str [expr $end+1] [expr $end+2]]
    append output [string range $str 0 [expr $end-1]][ctype char $chval]
    set str [string range $str [expr $end+3] end]
  }
  append output $str
  return $output
}
proc hexcode {str} {
  set output ""
  for {set i 0} {$i<[string length $str]} {incr i} {
    set char [string index $str $i]
    set cval [ctype ord $char]
    if {![regexp {[A-Za-z0-9]} $char]} { set char [format "%%%02x" $cval] }
    append output $char
  }
  return $output
}


set Port 9112
set URL "http://www.lbsd.k12.ms.us/~rkeene/tmp/rev-cgi.cgi"
#set URL "/~rkeene/r/rev-cgi.cgi"

proc putsock {sock data} {
	puts $sock $data
	flush $sock
}

proc HandleBlock {block handle pass {dest ""}} {
	global URL
	set sock [socket www.lbsd.k12.ms.us 80]
#	set sock [socket 127.0.0.1 80]
	set GET "GET ${URL}?key=[clock clicks][clock seconds]&handle=${handle}&pass=${pass}&data=[hexcode $block]"
	if {$dest!=""} { append GET "&dest=${dest}" }
	putsock $sock "$GET HTTP/1.0\n"
	set IsData 0
	set cnt 0
	set output ""
	while {1} {
		gets $sock ln
		if {$cnt==100} { break }
		if {!$IsData} { incr cnt }
		if {[regexp {^[0-9]*\:MY-tcl_SCRIPT_START$} $ln]} {
			set IsData 1
			set cnt 0
			continue
		}
		if {[regexp {^OKAY%0d%0a.*MY-tcl_SCRIPT_END$} $ln]} {
			set ln [string range $ln 10 end-17]
			set IsData -1
		} elseif {[regexp {^OKAY.*MY-tcl_SCRIPT_END$} $ln]} {
			set ln [string range $ln 4 end-17]
			set IsData -1
		} elseif {[regexp {MY-tcl_SCRIPT_END$} $ln]} {
			set ln [string range $ln 0 end-17]
			set IsData -1
		}
		if {$IsData!=0} {
			append output "[dehexcode $ln]"
		}
		if {$IsData==-1} { set IsData 0 }
		if {[eof $sock]} { break }
	}
	close $sock
	return $output
}

proc NewData {sock handle} {
	global Handles
	set ln [read $sock 8192]
	if {[eof $sock]} {
		unset Handles($Handles($sock))
		unset Handles($sock)
		return 
	}
	set ret [HandleBlock $ln $handle $handle]
	puts -nonewline $sock $ret
	flush $sock
}

proc NewConnection {sock addr port} {
	global URL Handles

	random seed [clock seconds]
	set handle [random 15152151]
	set Handles($handle) $sock
	set Handles($sock) $handle
	fconfigure $sock -blocking no -translation binary
	fileevent $sock readable "NewData $sock $handle"
	set ln [HandleBlock "" $handle $handle "127.0.0.1:22"]
	puts -nonewline $sock $ln
	flush $sock
}

proc WaitLoop {} {
	global Handles
	while {1} {
		update
		update idletasks
		set good 0
		foreach handle [lmatch [array names Handles] sock*] {
			set ret [HandleBlock "" $Handles($handle) $Handles($handle)]
			if {$ret==""} { continue }
			puts -nonewline $handle $ret
			flush $handle
			set good 1
			continue
		}
		if {$good} { continue }
		sleep 1
	}
}

after idle "WaitLoop"

socket -server NewConnection $Port
vwait forever
