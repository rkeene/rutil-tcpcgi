#!/usr/bin/tcl


proc extractargs {argv stdinlist envvar {list 0}} {
  global args
  foreach part [split "$argv&$stdinlist&$envvar" &] {
    if {$part!=""} {
      set wrk [split $part =]
      set name [dehexcode [lindex $wrk 0]]
      set value [dehexcode [lindex $wrk 1]]
      if {$list} { lappend lout [list $name $value] }
      if {![info exists args($name)] || $value!=""} { set args($name) $value }
    }
  }
  if {[info exists lout]} {
    return $lout
  } else {
    return ""
  }
}
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

if {[info exists env(QUERY_STRING)]} { set qs $env(QUERY_STRING) } else { set qs "" }
extractargs $argv [gets stdin] $qs

puts "Content-type: text/html\n\n"
if {![info exists args(handle)] || ![info exists args(pass)] || ![info exists args(data)]} {
	puts "No."
	return
}

catch { set sockfd [socket localhost 5906] }
if {![info exists sockfd]} {
	system daemon.tcl
	catch { set sockfd [socket localhost 5906] }
	if {![info exists sockfd]} { puts "Could not connect to daemon"; return }
}
if {[info exists args(dest)]} {
	puts $sockfd "CREATE $args(handle) $args(pass) $args(dest)"
	flush $sockfd
	gets $sockfd ln
	if {[lindex $ln 0]!="OKAY"} { puts "ERROR+$ln" ; return }
}
puts $sockfd "JOIN $args(handle) $args(pass)"
flush $sockfd
fconfigure $sockfd -blocking yes -translation binary -encoding binary -buffering none
fconfigure stdout -blocking no -translation binary -encoding binary -buffering none
set ln [gets $sockfd]
if {[lindex $ln 0]!="OKAY"} { puts "ERROR=$ln"; return }
set ln [gets $sockfd]
if {![string match "LEN=*" $ln]} { puts "ERROR, no len"; return }
set LEN [string range $ln 4 end]
set buf [read $sockfd $LEN]
puts stdout "[string length $buf]:MY-tcl_SCRIPT_START"
puts -nonewline "[hexcode $buf]"
flush stdout
puts stdout "MY-tcl_SCRIPT_END"
flush stdout
if {$args(data)!=""} { 
	puts -nonewline $sockfd $args(data)
	flush $sockfd
}
close $sockfd
