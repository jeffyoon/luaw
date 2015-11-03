webapp:registerResource {
    method = 'GET',
    path = 'address/:city/#zip',

    handler = function(httpConn)
        address = {
            city = httpConn.pathParams.city,
            zip = httpConn.pathParams.zip
        }
        httpConn:renderView('address-view.lua', address)
    end
}

