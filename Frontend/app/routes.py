from flask import request, render_template, json, session, redirect, url_for
from datetime import datetime
import hashlib, requests
from app import app

BASE_URL = "http://data.oracle.es:8080"

SCOPE_DESCRIPTION = {
    "age" : "Acceder a tu edad",
    "email" : "Ver tu dirección de correo",
    "family" : "Acceder al plan familiar",
    "name" : "Acceder a tu nombre",
    "surname" : "Acceder a tus apellidos"
}

ID_APP = {
    "3333" : "Spotify",
    "1234" : "Slack"
}

def login_endpoint():
    return "/login/challenge"

def get_user_endpoint(id):
    return "/user/{id}".format(id=id)

def query_params(queryMap):
    queryString = "?"
    for key, value in queryMap.items():
        queryString += "{key}={value}&".format(key=key, value=value)
    return queryString[:-1]

def extract_token(user, clientId):
    for token in user['tokens']:
        if(token['clientId'] == clientId):
            return token['token']

#viewfunctions

@app.route("/login", methods=['POST', 'GET'])
def login():

    if request.method == "GET": 

        queryMap = {
            "scope" : request.args.get("scope"),
            "client_id" : request.args.get("client_id")
        }

        not_scopeValues = SCOPE_DESCRIPTION.copy()
        scopeValues = []

        scopeQuery = request.args.get("scope")
        client_idQuery= request.args.get("client_id")
        scopeList = scopeQuery.split(",")
        for key in scopeList:
            scopeValues.append(SCOPE_DESCRIPTION[key])
            not_scopeValues.pop(key, None)

        return render_template(
            "login.html", 
            scopeValues=scopeValues,
            not_scopeValues=not_scopeValues,
            client_name=ID_APP[client_idQuery],
            queryParams=query_params(queryMap)
        )

    if request.method == "POST":   

        loginReq = {
            'email': request.form['email'], 
            'password': request.form['pass'],
        }

        queryMap = {
            "scope" : str(request.args.get("scope")),
            "client_id" : str(request.args.get("client_id"))
        }

        redirect_uri = str(request.args.get("redirect_uri"))

        response = requests.post(BASE_URL+login_endpoint()+query_params(queryMap), json = loginReq)
        if response.status_code == 202:
            user = response.json()
            print(user)
            return redirect(redirect_uri+"#access_token="+extract_token(user, queryMap['client_id'])+"&user_id="+str(user['id']))
                            
@app.route("/success/oracle", methods=["POST", "GET"])
def success_oracle():
    return render_template("success_oracles.html")


@app.route("/success/spotify", methods=["POST", "GET"])
def success_spotify():
    return render_template("success_spotify.html")

# COSAS UTILES
# date = datetime.now().strftime("%Y/%m/%d:%H:%M:%S")
# emailPassDate = rcvEmail+rcvPass+date
# tokenTemporal = hashlib.md5(emailPassDate.encode()).hexdigest()