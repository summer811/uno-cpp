const fs=require("fs");
const files=["server/server.h","server/server.cpp","server/game_logic.cpp","network.h","network.cpp"];
files.forEach(p=>{
    let b=fs.readFileSync(p);
    if(b[0]!==0xEF||b[1]!==0xBB||b[2]!==0xBF){
        fs.writeFileSync(p,Buffer.concat([Buffer.from([0xEF,0xBB,0xBF]),b]));
        console.log("BOM added: "+p);
    } else console.log("BOM ok: "+p);
});
