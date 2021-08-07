--TEST--
INI protection .min() + .policy_silent_fail()
--SKIPIF--
<?php if (!extension_loaded("snuffleupagus")) print("skip"); ?>
--INI--
sp.configuration_file={PWD}/config/sp-policy-silent-fail.ini
--FILE--
<?php 
var_dump(ini_set("log_errors_max_len", "199") === false);
var_dump(ini_get("log_errors_max_len"));
?>
--EXPECTF-- 
bool(true)
string(1) "0"