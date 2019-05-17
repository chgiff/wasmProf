#!/usr/bin/python3
import argparse
import os.path
import subprocess
import shutil
import time
import sys
from colorama import Fore, Back, Style

class cd:
    """Context manager for changing the current working directory"""
    def __init__(self, newPath):
        self.newPath = os.path.expanduser(newPath)
        if self.newPath == '':
            self.newPath = '.'

    def __enter__(self):
        self.savedPath = os.getcwd()
        os.chdir(self.newPath)

    def __exit__(self, etype, value, traceback):
        os.chdir(self.savedPath)

class RunInfo:
    originalBinarySize = 0
    originalTotalRuntime = 0
    wasmProfBinarySize = 0
    wasmProfTotalRuntime = 0
    wasabiBinarySize = 0
    wasabiTotalRuntime = 0

def printFlush(*objects):
    print(objects)
    sys.stdout.flush()


def CheckExt(choices):
    class Act(argparse.Action):
        def __call__(self,parser,namespace,fname,option_string=None):
            ext = os.path.splitext(fname)[1][1:]
            if ext not in choices:
                option_string = '({})'.format(option_string) if option_string else ''
                parser.error("file doesn't end with one of {}{}".format(choices,option_string))
            else:
                setattr(namespace,self.dest,fname)
    return Act

def runNode(nodeJSFile):
    with cd(os.path.dirname(nodeJSFile)):
        start = time.time()
        subprocess.check_call(["node", os.path.basename(nodeJSFile)])
        end = time.time()
        sys.stdout.flush()
    return end - start

def runWasmProf(wasmProfProg, wasmValidateProg, original_wasm, original_js, runInfo):
    #instrument code (force print on)
    subprocess.check_call([wasmProfProg, "-p", original_wasm])

    original_wasm_split = os.path.split(original_wasm)
    new_wasm = os.path.join(original_wasm_split[0], "prof_" + original_wasm_split[1])
    runInfo.wasmProfBinarySize = os.stat(new_wasm).st_size

    #validate new wasm file
    subprocess.check_call([wasmValidateProg, new_wasm])
    sys.stdout.flush()

    os.makedirs(os.path.join(original_wasm_split[0], "wasmProf_out"), exist_ok=True)
    shutil.move(new_wasm, os.path.join(original_wasm_split[0], "wasmProf_out/" + original_wasm_split[1])) #move wasm file
    new_js_moved = os.path.join(original_wasm_split[0], "wasmProf_out/" + os.path.basename(original_js))
    shutil.move(new_wasm + ".js", new_js_moved) #move generated js file

    #concatenate generated js and original js
    with open(original_js, "r+") as jsRead:
        with open(new_js_moved, "a+") as newJs:
            newJs.write(jsRead.read())

    #TODO run it
    runInfo.wasmProfTotalRuntime = runNode(new_js_moved)


def runWasabi(wasabiProg, wasmValidateProg, original_wasm, original_js, profile_js, runInfo):
    #instrument code
    subprocess.check_call([wasabiProg, "--hooks=call,return", original_wasm])
    sys.stdout.flush()

    #rename output directory
    wasabi_output_path = os.path.join(os.path.dirname(original_wasm), "wasabi_out")
    if(os.path.exists(wasabi_output_path)):
        shutil.rmtree(wasabi_output_path)
    shutil.move("out", wasabi_output_path)

    new_wasm = os.path.join(os.path.dirname(original_wasm), "wasabi_out", os.path.basename(original_wasm))
    runInfo.wasabiBinarySize = os.stat(new_wasm).st_size
    wasabi_js = os.path.splitext(new_wasm)[0] + ".wasabi.js"
    new_js = os.path.join(os.path.dirname(new_wasm), os.path.basename(original_js))

    #validate new wasm file
    subprocess.check_call([wasmValidateProg, new_wasm])
    sys.stdout.flush()

    #concatenate profiler js functions, generated js, and original js
    with open(new_js, "w+") as newJsFile:
        newJsFile.write("if(typeof module !== 'undefined' && module.exports){var performance = require('perf_hooks')['performance']; var Long = require('long');}\n")
        with open(wasabi_js, "r+") as wasabiJsFile:
            newJsFile.write(wasabiJsFile.read())
        if(profile_js != None):
            with open(profile_js, "r+") as profJsFile:
                newJsFile.write(profJsFile.read())
        with open(original_js, "r+") as origJsFile:
            newJsFile.write(origJsFile.read())

    #TODO run it
    runInfo.wasabiTotalRuntime = runNode(new_js)



def main():
    parser = argparse.ArgumentParser(description="Run instrumenting test")
    parser.add_argument('original_wasm', action=CheckExt({'wasm'}))
    parser.add_argument('original_js', action=CheckExt({'js'}))
    parser.add_argument('--wasmProf', default="wasmProf")
    parser.add_argument('--wasabi', default="wasabi")
    parser.add_argument('--wasm_validate', default="wasm_validate")
    parser.add_argument("--wasabi_js", default=None)
    args = parser.parse_args()

    #validate original wasm file
    #subprocess.check_call([args.wasm_validate, args.original_wasm])
    sys.stdout.flush()

    printFlush(args)
    runInfo = RunInfo()

    
    printFlush(Fore.RED + "\n\nRunning original program\n" + Style.RESET_ALL)
    runInfo.originalTotalRuntime = runNode(args.original_js)
    runInfo.originalBinarySize = os.stat(args.original_wasm).st_size

    printFlush(Fore.RED + "\n\nRunning wasmProf instrumented program\n" + Style.RESET_ALL)
    runWasmProf(args.wasmProf, args.wasm_validate, args.original_wasm, args.original_js, runInfo)

    printFlush(Fore.RED + "\n\nRunning wasabi instrumented program\n" + Style.RESET_ALL)
    runWasabi(args.wasabi, args.wasm_validate, args.original_wasm, args.original_js, args.wasabi_js, runInfo)

    printFlush("\n\nOriginal Binary Size: " + str(runInfo.originalBinarySize))
    printFlush("WasmProf Binary Size: " + str(runInfo.wasmProfBinarySize))
    printFlush("Wasabi Binary Size: " + str(runInfo.wasabiBinarySize))
    printFlush("\nOriginal Total Runtime: " + str(runInfo.originalTotalRuntime))
    printFlush("WasmProf Total Runtime: " + str(runInfo.wasmProfTotalRuntime))
    printFlush("Wasabi Total Runtime: " + str(runInfo.wasabiTotalRuntime))

if __name__ == "__main__":
    main()