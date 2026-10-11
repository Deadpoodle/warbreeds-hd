// Mark the program as built by Visual Studio. Old MSVC (4.x-5.x) executables have no Rich header,
// so Ghidra's PE loader cannot tell the compiler, and the RTTI analyzer and
// RecoverClassesFromRTTIScript then refuse to run ("only valid for Visual Studio windows PE").
// Run it once, before RecoverClassesFromRTTIScript.java:
//   analyzeHeadless <projDir> <projName> -process <program> -noanalysis -scriptPath <this folder>
//       -postScript MarkVisualStudio.java -postScript RecoverClassesFromRTTIScript.java
//@category WarBreeds

import ghidra.app.script.GhidraScript;
import ghidra.app.util.opinion.PeLoader;

public class MarkVisualStudio extends GhidraScript {
    @Override
    public void run() throws Exception {
        String vs = PeLoader.CompilerOpinion.CompilerEnum.VisualStudio.toString();
        println("compiler was '" + currentProgram.getCompiler() + "', now '" + vs + "'");
        currentProgram.setCompiler(vs);
    }
}
