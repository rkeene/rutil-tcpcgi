#!/usr/bin/tcl

if {[lindex $argv 0]==""} { set Port "5906" } else { set Port [lindex $argv 0] }


####
proc NewConnection {sock addr port} {
	fileevent $sock readable "ProcessData $sock $addr $port"
}

proc ProcessData {sock addr port} {
	global Forward Conns Outbound Handles Engaged Buff

	if {[eof $sock]} { close $sock; set eofd 1 }

	if {[info exists Forward($sock)]} {
		set src $sock
		set dest $Forward($sock)
		if {[info exists Outbound($sock)]} { set outbound "1" } else { set outbound "0" }
		if {$outbound=="1"} { set handle $Handles($dest) } else { set handle $Handles($src) }
		catch {
			if {[info exists eofd]} {
# if we get EOF from a connection outbound, kill everyone
# including the handle
				if {[info exists Outbound($sock)]} {
					catch { close $dest }
					catch { unset Handles($dest) }
					catch { unset Engaged($src) }
					catch { unset Outbound($sock) }
					catch { unset Conns($handle) }
				} else {
					fileevent $dest readable ""
					unset Engaged($dest)
				}
				catch { unset Forward($src) }
				catch { unset Forward($dest) }
				return
			}
			append Buff($handle.$outbound) [read $src]
			puts -nonewline $dest $Buff($handle.$outbound)
			if {[catch { flush $dest }]} {
				puts "Error flushing, holding it"
			} else {
				unset Buff($handle.$outbound)
			}
		} err
		if {$err!=""} {
			puts "Error in background: $err"
		}
		
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
			if {[info exists realpass]} { puts $sock "ERROR$handle Handle exists"; flush $sock; return }
			set daddr [split $data :]
			set dport [lindex $daddr 1]
			set daddr [lindex $daddr 0]
			catch { set newsock [socket $daddr $dport] }
			if {![info exists newsock]} { puts $sock "ERROR$handle Cannot create"; flush $sock; return }
			set Outbound($newsock) $addr:$port
#			set Forward($sock) $newsock
#			set Forward($newsock) $sock
#			set Handles($sock) $handle
			set Conns($handle) [list $pass $newsock]
#			fconfigure $newsock -blocking no -translation binary
#			fconfigure $sock -blocking no -translation binary
#			fileevent $newsock readable "ProcessData $newsock $daddr $dport"
			puts $sock "OKAY$handle"
			flush $sock
			return
		}
		"CLOSE" {
		}
		"JOIN" {
			if {![info exists realpass]} {
				puts $sock "ERROR$handle Invalid handle"
				flush $sock
				return
			}
			if {$pass!=$realpass} {
puts "$pass -- $realpass"
				puts $sock "Invalid password"
				close $sock
				return
			}
			set newsock [lindex $Conns($handle) 1]
			if {[info exists Engaged($newsock)]} { puts $sock "ERROR$handle is engaged"; flush $sock; return }
			set Forward($sock) $newsock
			set Forward($newsock) $sock
			set Handles($sock) $handle
			set Engaged($newsock) 1
			fconfigure $newsock -blocking no -translation binary
			fconfigure $sock -blocking no -translation binary
			fileevent $newsock readable "ProcessData $newsock 0 0"
		}
	}
}

if {[fork]!=0} { wait;return  }
if {[fork]!=0} { return }
socket -server NewConnection $Port
vwait forever
