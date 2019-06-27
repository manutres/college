package es.um.ssdd;

import javax.ws.rs.GET;
import javax.ws.rs.Path;
import javax.ws.rs.Produces;
import javax.ws.rs.core.MediaType;

/**
 * Root resource (exposed at "myresource" path)
 */
@Path("/")
public class MyResource {

    /**
     * Method handling HTTP GET requests. The returned object will be sent
     * to the client as "text/plain" media type.
     *
     * @return String that will be returned as a text/plain response.
     */
    @GET
    @Path("/item")
    @Produces(MediaType.APPLICATION_JSON, MediaType.TEXT_XML)
    public String getItem() {
        return "Got it!";
    }


    /**
     * para empaquetar en el contenedor:
     * nos vamos a la raiz del proyecto
     * hacemos un package con maven para que nos genere el .war
     * creamos un dockerfile
     */
}
