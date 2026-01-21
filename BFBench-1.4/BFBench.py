#BFBench 1.4 by David Catt
import sys
import subprocess
import datetime
import threading

class bfinterpreter:
    def __init__(self, name, prog, args, useio):
        self.name = name
        self.prog = prog
        self.args = args
        self.useio = useio
class bftest:
    def __init__(self, name, bfile, binput, boutput, timeout):
        self.name = name
        self.file = bfile
        self.input = binput
        self.output = boutput
        self.timeout = timeout
    def getinput(self, inputfile):
        with open(inputfile, "r") as din:
            self.input = din.read()
    def getoutput(self, outputfile):
        with open(outputfile, "r") as dout:
            self.output = dout.read()
class bfresult:
    def __init__(self, retcode, ptime, match):
        self.exitcode = retcode
        self.timetaken = ptime
        self.goodoutput = match
    def printdata(self):
        sys.stdout.write("[RCode=" + str(self.exitcode) + " RTime=" + str(self.timetaken) + " OGood=" + str(self.goodoutput) + "]")
class bfbench:
    def __init__(self):
        self.interpreters = []
        self.tests = []
    def addinterpreter(self, interp):
        self.interpreters.append(interp)
    def addtest(self, test):
        self.tests.append(test)
    def runall(self):
        for bfi in self.interpreters:
            sys.stdout.write(bfi.name + " ###")
            for tst in self.tests:
                sys.stdout.write(" " + tst.name)
                self.bfrun(bfi, tst).printdata()
            print ""
    def bfrun(self, interp, test):
        r = 0
        i = ""
        o = ""
        e = ""
        p = None
        d = datetime.datetime.now()
        if interp.useio:
            p = subprocess.Popen([interp.prog] + interp.args, stdin=subprocess.PIPE, stdout=subprocess.PIPE, universal_newlines = True)
            with open(test.file, "r") as pin:
                i += pin.read() + "!"
        else:
            p = subprocess.Popen([interp.prog] + interp.args + [test.file], stdin=subprocess.PIPE, stdout=subprocess.PIPE, universal_newlines = True)
        i += test.input
        o,e = self.pcommunicate(p, i, test.timeout)
        return bfresult(p.returncode, datetime.datetime.now() - d, o == test.output)
    def pcommunicate(self, proc, indata, timeout):
        self._p = proc
        self._i = indata
        self._o = ""
        self._e = ""
        def pcommunicate_thread():
            self._o,self._e = self._p.communicate(self._i)
        t = threading.Thread(target = pcommunicate_thread)
        t.start()
        t.join(timeout)
        if t.is_alive():
            proc.terminate()
        return self._o,self._e


#Create benchmark
bench = bfbench()
#Add interpreters
bench.addinterpreter(bfinterpreter("bff", "bff.exe", [], False))
bench.addinterpreter(bfinterpreter("bff4", "bff4.exe", [], True))
bench.addinterpreter(bfinterpreter("bff4 -DNOLNR", "bff4_nolnr.exe", [], True))
bench.addinterpreter(bfinterpreter("bffsree", "bffsree_gcc.exe", [], False))
bench.addinterpreter(bfinterpreter("bffsree_ref", "bffsree_ref.exe", [], False))
bench.addinterpreter(bfinterpreter("bfli", "bfli.exe", [], False))
bench.addinterpreter(bfinterpreter("bfli slow", "bfli.exe", ["-slow"], False))
bench.addinterpreter(bfinterpreter("bfref", "bfref.exe", [], False))
bench.addinterpreter(bfinterpreter("FBI 1.2", "FBI.exe", [], False))
bench.addinterpreter(bfinterpreter("stackbfi", "stackbfi.exe", [], True))
bench.addinterpreter(bfinterpreter("yabi", "yabi.exe", [], False))
#Add tests
tmp = bftest("Mandelbrot", "mandelbrot.b", "", "", 200)
tmp.getoutput("mandelbrot.out")
bench.addtest(tmp)
bench.addtest(bftest("Factoring", "factor.b", "123456789123456789\n", "123456789123456789: 3 3 7 11 13 19 3607 3803 52579\n", 50))
tmp = bftest("Long_Run", "long.b", "", "", 200)
tmp.getoutput("long.out")
bench.addtest(tmp)
bench.addtest(bftest("Self_Interpret", "Bootstrap.b", ">>>+[[-]>>[-]++>+>+++++++[<++++>>++<-]++>>+>+>+++++[>++>++++++<<-]+>>>,<++[[>[->>]<[>>]<<-]<[<]<+>>[>]>[<+>-[[<+>-]>]<[[[-]<]++<-[<+++++++++>[<->-]>>]>>]]<<]<]<[[<]>[[>]>>[>>]+[<<]<[<]<+>>-]>[>]+[->>]<<<<[[<<]<[<]+<<[+>+<<-[>-->+<<-[>+<[>>+<<-]]]>[<+>-]<]++>>-->[>]>>[>>]]<<[>>+<[[<]<]>[[<<]<[<]+[-<+>>-[<<+>++>-[<->[<<+>>-]]]<[>+<-]>]>[>]>]>[>>]>>]<<[>>+>>+>>]<<[->>>>>>>>]<<[>.>>>>>>>]<<[>->>>>>]<<[>,>>>]<<[>+>]<<[+<<]<]!>+++++++++[<++++++++>-]<.>+++++++[<++++>-]<+.+++++++..+++.>>>++++++++[<++++>-]<.>>>++++++++++[<+++++++++>-]<---.<<<<.+++.------.--------.>>+.!", "Hello World!", 200))
bench.addtest(bftest("Golden_Ratio", "golden.b", "", "1.618033988749894848204586834365638117", 5))
tmp = bftest("Hanoi", "hanoi.b", "", "", 80)
tmp.getoutput("hanoi.out")
bench.addtest(tmp)
tmp = bftest("99_Bottles_Of_Beer", "beer.b", "", "", 5)
tmp.getoutput("beer.out")
bench.addtest(tmp)
bench.addtest(bftest("Simple_Benchmark", "bench.b", "", "OK", 5))
#Run benchmark
bench.runall()
