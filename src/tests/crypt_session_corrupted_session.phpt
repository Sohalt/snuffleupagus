--TEST--
Set a custom session handler
--SKIPIF--
<?php if (!extension_loaded("snuffleupagus")) die "skip"; ?>
--INI--
sp.configuration_file={PWD}/config/config_crypt_session.ini
session.save_path = "/tmp"
--ENV--
return <<<EOF
REMOTE_ADDR=127.0.0.1
EOF;
--FILE--
<?php 

session_start();			// Start new_session , it will read an empty session
$_SESSION["tete"] = "titi"; // Encrypt and write the session
$id = session_id(); 		// Get the session_id to use it later
$filename = session_save_path() . '/sess_' . $id;
session_write_close();

$file_handle = fopen($filename, 'w'); 
fwrite($file_handle, 'toto|s:4:"tata";');
fclose($file_handle);

session_id($id);
session_start();
var_dump($_SESSION);
?>
--EXPECTF--
Fatal error: [snuffleupagus][cookie_encryption] Buffer underflow tentative detected in cookie encryption handling in %s/tests/crypt_session_corrupted_session.php on line %s
