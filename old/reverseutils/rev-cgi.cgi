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

if {[info exists env(QUERY_STRING)]} { set qs $env(QUERY_STRING) } else { set qs "" }
extractargs $argv [gets stdin] $qs

puts "Content-type: text/html\n"
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
fconfigure $sockfd -blocking no -translation binary
if {[info exists args(dest)]} {
	puts $sockfd "CREATE $args(handle) $args(pass) $args(dest)"
	flush $sockfd
	gets $sockfd ln
}
puts $sockfd "JOIN $args(handle) $args(pass)"
flush $sockfd
puts -nonewline [read $sockfd]
flush stdout
if {$args(data)!=""} { 
	puts -nonewline $sockfd $args(data)
	flush $sockfd
}
