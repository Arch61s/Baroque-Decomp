// Offline evidence exporter for Baroque SLUS_217.14. Output is unreviewed pseudocode, not game source.
import ghidra.GhidraApplicationLayout;
import ghidra.framework.Application;
import ghidra.framework.HeadlessGhidraApplicationConfiguration;
import ghidra.base.project.GhidraProject;
import ghidra.program.model.listing.*;
import ghidra.program.model.lang.*;
import ghidra.program.model.address.*;
import ghidra.program.util.DefaultLanguageService;
import ghidra.program.flatapi.FlatProgramAPI;
import ghidra.app.decompiler.*;
import ghidra.util.task.TaskMonitor;
import java.nio.file.*;
import java.io.*;
import java.util.*;

public class BaroqueDecompile {
 public static void main(String[] args) throws Exception {
  Path root=Paths.get(args[0]);
  Application.initializeApplication(new GhidraApplicationLayout(new File(args[1])),new HeadlessGhidraApplicationConfiguration());
  Path projects=root.resolve("work/ghidra"); Files.createDirectories(projects);
  boolean exists=Files.exists(projects.resolve("Baroque.gpr"));
  GhidraProject project=exists?GhidraProject.openProject(projects.toString(),"Baroque",false):GhidraProject.createProject(projects.toString(),"Baroque",false);
  try {
   Program program;
   if(exists) program=project.openProgram("/","SLUS_217.14",false);
   else {
    Language lang=DefaultLanguageService.getLanguageService().getLanguage(new LanguageID("r5900:LE:32:default"));
    program=project.importProgram(root.resolve("work/disc/SLUS_217.14").toFile(),lang,lang.getDefaultCompilerSpec());
    project.saveAs(program,"/","SLUS_217.14",true);
    System.out.println("Imported original ELF for offline static analysis");
   }
   if(!Files.exists(root.resolve("work/ghidra_analysis_complete.txt"))) {
    System.out.println("Analyzing original functions");
    GhidraProject.analyze(program); project.save(program);
    Files.writeString(root.resolve("work/ghidra_analysis_complete.txt"),"Analysis completed\n");
   }
   Set<Long> selected=new HashSet<>();
   List<String> argList=Arrays.asList(args);
   boolean all=argList.contains("--all");
   boolean listOnly=argList.contains("--list");
   if(args.length>2) {
    FlatProgramAPI api=new FlatProgramAPI(program);
    int transaction=program.startTransaction("Create requested functions");
    boolean changed=false;
    try {
     for(int i=2;i<args.length;i++) {
      if(args[i].startsWith("--"))continue;
      long address=Long.parseUnsignedLong(args[i],16);selected.add(address);
      Address entry=program.getAddressFactory().getDefaultAddressSpace().getAddress(address);
      if(program.getFunctionManager().getFunctionAt(entry)==null) { api.disassemble(entry);api.createFunction(entry,null);changed=true; }
     }
    } finally { program.endTransaction(transaction,true); }
    if(changed) project.save(program);
   }
   Path out=root.resolve("work/decompiled"); Files.createDirectories(out);
   List<Function> functions=new ArrayList<>();
   program.getFunctionManager().getFunctions(true).forEach(f->{long a=f.getEntryPoint().getOffset();if(all||listOnly||selected.contains(a))functions.add(f);});
   if(listOnly) {
    try(BufferedWriter index=Files.newBufferedWriter(out.resolve("function_list.tsv"))) {
     for(Function f:functions) index.write(String.format("%08x\t%s\t%d\n",f.getEntryPoint().getOffset(),f.getName(),f.getBody().getNumAddresses()));
    }
    try(BufferedWriter sx=Files.newBufferedWriter(out.resolve("string_xrefs.tsv"))) {
     for(Data d:program.getListing().getDefinedData(true)) {
      if(!d.hasStringValue())continue;
      String s=String.valueOf(d.getValue()).replace("\t"," ").replace("\n","\\n").replace("\r","");
      for(var r:program.getReferenceManager().getReferencesTo(d.getAddress())) {
       Function f=program.getFunctionManager().getFunctionContaining(r.getFromAddress());
       sx.write(String.format("%08x\t%08x\t%s\t%s\n",d.getAddress().getOffset(),r.getFromAddress().getOffset(),f==null?"-":String.format("%08x",f.getEntryPoint().getOffset()),s));
      }
     }
    }
    try(BufferedWriter cg=Files.newBufferedWriter(out.resolve("call_graph.tsv"))) {
     for(Function f:functions) for(Function callee:f.getCalledFunctions(TaskMonitor.DUMMY)) cg.write(String.format("%08x\t%08x\n",f.getEntryPoint().getOffset(),callee.getEntryPoint().getOffset()));
    }
    System.out.println("Listed "+functions.size()+" functions");
    return;
   }
   DecompInterface dc=new DecompInterface(); dc.toggleCCode(true); dc.toggleSyntaxTree(false);
   if(!dc.openProgram(program)) throw new IOException(dc.getLastMessage());
   int done=0,successful=0,failed=0;
   try(BufferedWriter index=Files.newBufferedWriter(out.resolve(all?"all_index.tsv":"selected_index.tsv"),StandardOpenOption.CREATE,all?StandardOpenOption.TRUNCATE_EXISTING:StandardOpenOption.APPEND)) {
    for(Function f:functions) {
     long address=f.getEntryPoint().getOffset();
     Path output=out.resolve(String.format("%08x.c",address));
     if(all&&Files.exists(output)) {successful++;done++;index.write(String.format("%08x\t%s\t%d\n",address,f.getName(),f.getBody().getNumAddresses()));continue;}
     DecompileResults result=dc.decompileFunction(f,30,TaskMonitor.DUMMY);
     if(result.decompileCompleted()&&result.getDecompiledFunction()!=null) {
      Files.writeString(output,"/* Unreviewed static pseudocode; original address "+f.getEntryPoint()+". */\n"+result.getDecompiledFunction().getC());
      successful++;Files.deleteIfExists(Paths.get(output+".error.txt"));
      index.write(String.format("%08x\t%s\t%d\n",address,f.getName(),f.getBody().getNumAddresses())); index.flush();
     } else {failed++;Files.writeString(Paths.get(output+".error.txt"),String.valueOf(result.getErrorMessage()));}
     if(++done%200==0) System.out.println("Exported "+done+" / "+functions.size());
    }
   } finally { dc.dispose(); }
   Files.writeString(out.resolve(all?"all_export_summary.txt":"last_export_summary.txt"),"Attempted: "+done+"\nSuccessful: "+successful+"\nFailed: "+failed+"\nUnreviewed pseudocode; not a completed native port.\n");
   System.out.println("Offline export completed: "+successful+" successful / "+done+" attempted; "+failed+" failed");
  } finally { project.close(); }
 }
}
