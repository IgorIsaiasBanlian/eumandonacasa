
function VerificaEstado(){
    var porta2_estado = document.getElementById("porta2_estado").innerHTML;
    var porta2_titulo = document.getElementById("porta2_titulo").innerHTML;
    
    var porta3_estado = document.getElementById("porta3_estado").innerHTML;
    var porta3_titulo = document.getElementById("porta3_titulo").innerHTML;

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

document.getElementsByTagName("body")[0].innerHTML += "<a href='http://www.robocore.net' id='float-image'></a>";

}
