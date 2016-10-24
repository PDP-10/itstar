/*

  Command opcodes used to talk to the TAPESRV program running under MTS on the
  IBM ES/9000 (the only machine around here with nice 9-track tape handling).

  03/13/95	JMBW	Created.

  Protocol is as follows:

  Connect to server (port # is not fixed, must be discovered by telneting to
  the IBM and watching what TAPESRV says when it's started -- you need to be
  talking to the IBM anyway so you can $MOUNT the tape).

  Then, for each command, the client sends a 2-byte command code (LSB first).
  Command codes are all below the industry standard minimum record length of
  18;  if the code is .GE. 18 then the command is "write a record", and the
  length code is followed by that number of bytes of data to be written.

  The server replies to each command with a one-byte return status, which is
  X'00' for success and X'FF' for failure.  In the case of the "read a record"
  command, a successful return code will be followed by a 16-bit record length
  (LSB first), followed by that number of data bytes.  If a record is too large
  for a receiving process's buffer, then disposing of the extra bytes is the
  receiving process's problem.

*/

#define TS_WTM 0	/* WRITE TAPE MARK */
#define TS_RDR 1	/* READ A RECORD */
#define TS_CLS 2	/* CLOSE CONNECTION */
#define TS_REW 3	/* REWIND */
#define TS_BSF 4	/* BACKWARD SPACE FILE */
#define TS_BSR 5	/* BACKWARD SPACE RECORD */
#define TS_EOT 6	/* SPACE TO LEOT (BETWEEN THE TWO TAPE MARKS) */
#define TS_FSF 7	/* FORWARD SPACE FILE */
#define TS_FSR 8	/* FORWARD SPACE RECORD */
