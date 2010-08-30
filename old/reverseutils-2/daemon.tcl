#!/usr/bin/tclsh

package require Tclx

if {[lindex $argv 0]==""} { set Port "5906" } else { set Port [lindex $argv 0] }


####
proc NewConnection {sock addr port} {
	fileevent $sock readable "ProcessData $sock $addr $port"
}

proc ProcessData {sock addr port} {
	global Forward Conns Outbound Handles Engaged Buff

	if {[eof $sock]} { close $sock; set eofd 1 }
	if {[info exists Outbound($sock)]} { set outbound "1" } else { set outbound "0" }

	if {[info exists Forward($sock)]} {
		set src $sock
		set dest $Forward($sock)
		if {$outbound=="1"} { set handle $Handles($dest) } else { set handle $Handles($src) }
			if {[info exists eofd]} {
# if we get EOF from a connection outbound, kill everyone
# including the handle
				if {[info exists Outbound($sock)]} {
					if {[info exists Buff(${handle}.${outbound})]} {
						puts $dest $Buff(${handle}.${outbound})
						unset Buff(${handle}.${outbound})
						flush $dest
					}
					catch { close $dest }
					catch { unset Handles($dest) }
					catch { unset Engaged($src) }
					catch { unset Outbound($sock) }
					catch { unset Conns($handle) }
				} else {
					unset Engaged($dest)
					fileevent $dest readable ""
				}
				catch { unset Forward($src) }
				catch { unset Forward($dest) }
				return
			}

			fconfigure $src -blocking no -translation binary -buffering none -encoding binary
			set data [read $src 8192]
			if {$outbound=="0"} {
				if {[info exists Buff(${handle}.${outbound})]} {
					puts -nonewline $dest "$Buff(${handle}.${outbound})"
					unset Buff(${handle}.${outbound})
				}
				if {$data==""} { return }
				puts -nonewline $dest $data
				flush $dest
				ProcessData $dest 0 0
			} else {
				flush $src
				append Buff($handle.$outbound) $data
			}
#			fconfigure $dest -blocking no -translation binary -buffering none -encoding binary
#			set data1 [read $dest 8192]
#			append Buff($handle.${outbound}) $data1

#			puts -nonewline $dest $Buff($handle.$outbound)
#			if {[catch { flush $dest }]} {
#				puts "Error flushing, holding it"
#			} else {
#				unset Buff($handle.$outbound)
#			}
		
		return
	}
	if {[info exists eofd]} { return }
	gets $sock ln
	set ln [split $ln \ ]
	set cmd [string toupper [lindex $ln 0]]
	set handle [lindex $ln 1]
	set pass [lindex $ln 2]
	set data [lindex $ln 3]

	if {[info exists Conns($handle)]} { set realpass [lindex $Conns($handle) 0] }
	switch -- $cmd {
		"CREATE" {
			if {[info exists realpass]} { puts $sock "ERROR $handle Handle exists"; flush $sock; return }
			set daddr [split $data :]
			set dport [lindex $daddr 1]
			set daddr [lindex $daddr 0]
			catch { set newsock [socket $daddr $dport] }
			if {![info exists newsock]} { puts $sock "ERROR $handle Cannot create"; flush $sock; return }
			set Outbound($newsock) $addr:$port
#			set Forward($sock) $newsock
			set Forward($newsock) $sock
			set Handles($sock) $handle
			set Conns($handle) [list $pass $newsock]
			fconfigure $newsock -blocking no -translation binary -buffering none -encoding binary
#			fconfigure $sock -blocking no -translation binary
			fileevent $newsock readable "ProcessData $newsock $daddr $dport"
			puts $sock "OKAY $handle"
			flush $sock
			return
		}
		"CLOSE" {
			if {![info exists realpass]} { puts $sock "ERROR $handle Handle does not exists"; flush $sock; return }
			if {$pass!=$realpass} {
				puts $sock "ERROR $handle Invalid password"
				close $sock
				return
			}
			set newsock [lindex $Conns($handle) 1]
			if {[info exists Engaged($newsock)]} { puts $sock "ERROR $handle is engaged"; flush $sock; return }
			catch { close $newsock }
			catch { unset Handles($sock) }
			catch { unset Engaged($newsock) }
			catch { unset Outbound($sock) }
			catch { unset Conns($handle) }
	
			puts $sock "OKAY $handle"
			flush $sock
			return
		}
		"JOIN" {
			if {![info exists realpass]} {
				puts $sock "ERROR $handle Invalid handle"
				flush $sock
				return
			}
			if {$pass!=$realpass} {
				puts $sock "ERROR $handle Invalid password"
				close $sock
				return
			}
			flush $sock
			set newsock [lindex $Conns($handle) 1]
			if {[info exists Engaged($newsock)]} { puts $sock "ERROR $handle is engaged"; flush $sock; return }
			puts $sock "OKAY"
			flush $sock
			set Forward($sock) $newsock
			set Forward($newsock) $sock
			set Handles($sock) $handle
			set Engaged($newsock) 1
			fconfigure $newsock -blocking no -translation binary -buffering none -encoding binary
			fconfigure $sock -blocking no -translation binary -buffering none -encoding binary
			if {[info exists Buff(${handle}.1)]} { 
				puts $sock "LEN=[string length $Buff(${handle}.1)]"
				puts -nonewline $sock $Buff(${handle}.1)
				catch { flush $sock; set flushed 1 }
				if {[info exists flushed]} {
					unset Buff(${handle}.1)
				}
			} else {
				puts $sock "LEN=0"
				flush $sock
			}
			fconfigure $newsock -blocking no -translation binary -buffering none -encoding binary
			fileevent $newsock readable "ProcessData $newsock 0 0"
			fconfigure $newsock -blocking no -translation binary -buffering none -encoding binary
		}
	}
}

if {[fork]!=0} { wait;return  }
if {[fork]!=0} { return }
socket -server NewConnection $Port
vwait forever
