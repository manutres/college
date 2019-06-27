package com.example.whatsapp;

import androidx.appcompat.app.AppCompatActivity;
import androidx.recyclerview.widget.LinearLayoutManager;
import androidx.recyclerview.widget.RecyclerView;

import android.app.DownloadManager;
import android.database.Cursor;
import android.os.Bundle;
import android.provider.ContactsContract;
import android.telephony.TelephonyManager;

import java.util.ArrayList;

public class BuscarUsuarios extends AppCompatActivity {

    private RecyclerView listUsuarios;
    private RecyclerView.Adapter listUsuariosAdapter;
    private RecyclerView.LayoutManager listUsuariosLayaout;

    ArrayList<Usuario> usuarios, listaContactos;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_buscar_usuarios);
        usuarios = new ArrayList<>();
        usuarios.add(new Usuario(" ","eduardo", "686-52-49-08"));
        listaContactos = new ArrayList<>();

        startRecyclerView();
        getListaUsuarios();
    }

    private void getListaUsuarios(){

        String prefijoISO = "";

        Cursor telefonos = getContentResolver().query(ContactsContract.CommonDataKinds.Phone.CONTENT_URI,null,null,null,null);
        while(telefonos.moveToNext()){

            String nombre = telefonos.getString(telefonos.getColumnIndex(ContactsContract.CommonDataKinds.Phone.DISPLAY_NAME));
            String telefono = telefonos.getString(telefonos.getColumnIndex(ContactsContract.CommonDataKinds.Phone.NUMBER));

            telefono = telefono.replace(" ", "");
            telefono = telefono.replace("-", "");
            telefono = telefono.replace("(", "");
            telefono = telefono.replace(")", "");

            if(!String.valueOf(telefono.charAt(0)).equals("+"))
                telefono = prefijoISO + telefono;


            Usuario usuario = new Usuario("", nombre, telefono);
            listaContactos.add(usuario);
            listUsuariosAdapter.notifyDataSetChanged();

            getDetallesUsuario(usuario);

        }
    }

    private void getDetallesUsuario(Usuario usuario) {
         //video 7 min 25 y y video 8
        //Usuario user = new Usuario("", )

    }

    private String getNumeroISO(){
        String iso = null;
        TelephonyManager telefonoManager = (TelephonyManager) getApplicationContext().getSystemService(getApplicationContext().TELEPHONY_SERVICE);

        if(!telefonoManager.getNetworkCountryIso().equals(""))
            iso = telefonoManager.getNetworkCountryIso();

        return PrefijoTelefonoISO.getPhone(iso);
    }


    private void startRecyclerView() {
        listUsuarios = findViewById(R.id.listUsuario);
        listUsuarios.setNestedScrollingEnabled(false);
        listUsuarios.setHasFixedSize(false);

        //probable errores
        listUsuariosLayaout = new LinearLayoutManager(getApplicationContext(), RecyclerView.VERTICAL, false);
        listUsuarios.setLayoutManager(listUsuariosLayaout);

        listUsuariosAdapter = new ListaUsuarioAdaptador(usuarios);
        listUsuarios.setAdapter(listUsuariosAdapter);

    }
}
