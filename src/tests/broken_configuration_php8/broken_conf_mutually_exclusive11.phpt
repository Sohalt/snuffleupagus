--TEST--
Broken configuration - ret and var are mutually exclusives
--SKIPIF--
<?php if (!extension_loaded("snuffleupagus")) print "skip"; ?>
<?php if (PHP_VERSION_ID < 80000) print "skip"; ?>
--INI--
sp.configuration_file={PWD}/config/broken_conf_mutually_exclusive11.ini
--FILE--
--EXPECTF--
Fatal error: [snuffleupagus][0.0.0.0][config][log] Invalid configuration line for '%a': '.ret' and '.var' are mutually exclusive on line 1 in Unknown on line 0

Fatal error: [snuffleupagus][0.0.0.0][config][log] Invalid configuration file in Unknown on line 0
Could not startup.
