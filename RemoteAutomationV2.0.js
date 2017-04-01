
function VerificaEstado(){
    var porta2_estado = document.getElementById("porta2_estado").innerHTML;
    var porta2_titulo = document.getElementById("porta2_titulo").innerHTML;
    
    var porta3_estado = document.getElementById("porta3_estado").innerHTML;
    var porta3_titulo = document.getElementById("porta3_titulo").innerHTML;
    
    var porta4_estado = document.getElementById("porta4_estado").innerHTML;
    var porta4_titulo = document.getElementById("porta4_titulo").innerHTML;
    
    var porta5_estado = document.getElementById("porta5_estado").innerHTML;
    var porta5_titulo = document.getElementById("porta5_titulo").innerHTML;
    
    var porta6_estado = document.getElementById("porta6_estado").innerHTML;
    var porta6_titulo = document.getElementById("porta6_titulo").innerHTML;
    
    var porta7_estado = document.getElementById("porta7_estado").innerHTML;
    var porta7_titulo = document.getElementById("porta7_titulo").innerHTML;

    var porta8_estado = document.getElementById("porta8_estado").innerHTML;
    var porta8_titulo = document.getElementById("porta8_titulo").innerHTML;
    
    var porta9_estado = document.getElementById("porta9_estado").innerHTML;
    var porta9_titulo = document.getElementById("porta9_titulo").innerHTML;

    if(porta2_estado === "0"){
        document.getElementById("porta2_botao").innerHTML="<div class='porta_desligada'></div><a href='/?l2' class='botao'>"+porta2_titulo+"</a>";
    } else {
        document.getElementById("porta2_botao").innerHTML="<div class='porta_ligada'></div><a href='/?d2' class='botao'>"+porta2_titulo+"</a>";
    }

    if(porta3_estado === "0"){
        document.getElementById("porta3_botao").innerHTML="<div class='porta_desligada'></div><a href='/?l3' class='botao'>"+porta3_titulo+"</a>";
    } else {
        document.getElementById("porta3_botao").innerHTML="<div class='porta_ligada'></div><a href='/?d3' class='botao'>"+porta3_titulo+"</a>";
    }
    
    if(porta4_estado === "0"){
        document.getElementById("porta4_botao").innerHTML="<div class='porta_desligada'></div><a href='/?l4' class='botao'>"+porta4_titulo+"</a>";
    } else {
        document.getElementById("porta4_botao").innerHTML="<div class='porta_ligada'></div><a href='/?d4' class='botao'>"+porta4_titulo+"</a>";
    }
    
    if(porta5_estado === "0"){
        document.getElementById("porta5_botao").innerHTML="<div class='porta_desligada'></div><a href='/?l5' class='botao'>"+porta5_titulo+"</a>";
    } else {
        document.getElementById("porta5_botao").innerHTML="<div class='porta_ligada'></div><a href='/?d5' class='botao'>"+porta5_titulo+"</a>";
    }
    
    if(porta6_estado === "0"){
        document.getElementById("porta6_botao").innerHTML="<div class='porta_desligada'></div><a href='/?l6' class='botao'>"+porta6_titulo+"</a>";
    } else {
        document.getElementById("porta6_botao").innerHTML="<div class='porta_ligada'></div><a href='/?d6' class='botao'>"+porta6_titulo+"</a>";
    }
    
    if(porta7_estado === "0"){
        document.getElementById("porta7_botao").innerHTML="<div class='porta_desligada'></div><a href='/?l7' class='botao'>"+porta7_titulo+"</a>";
    } else {
        document.getElementById("porta7_botao").innerHTML="<div class='porta_ligada'></div><a href='/?d7' class='botao'>"+porta7_titulo+"</a>";
    }
    
    if(porta8_estado === "0"){
        document.getElementById("porta8_botao").innerHTML="<div class='porta_desligada'></div><a href='/?l8' class='botao'>"+porta8_titulo+"</a>";
    } else {
        document.getElementById("porta8_botao").innerHTML="<div class='porta_ligada'></div><a href='/?d8' class='botao'>"+porta8_titulo+"</a>";
    }
    
    if(porta9_estado === "0"){
        document.getElementById("porta9_botao").innerHTML="<div class='porta_desligada'></div><a href='/?l9' class='botao'>"+porta9_titulo+"</a>";
    } else {
        document.getElementById("porta9_botao").innerHTML="<div class='porta_ligada'></div><a href='/?d9' class='botao'>"+porta9_titulo+"</a>";
    }

document.getElementsByTagName("body")[0].innerHTML += "<a href='http://www.robocore.net' id='float-image'></a>";

}
 