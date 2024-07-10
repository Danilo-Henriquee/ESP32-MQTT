let wifiQuality = -30;

const networkButton = document.querySelector("#network-save");
const mqttButton = document.querySelector("#mqtt-save");
const applyBytton = document.querySelector("#apply-changes");

const dhcp = document.querySelector("#dhcp");

let __IPADDRESS__ = "__IPADDRESS_VALUE__";

networkButton.addEventListener("click", () => sendData("network", 7));

mqttButton.addEventListener("click", () => sendData("mqtt", 7));

applyBytton.addEventListener("click", () => {
    if (confirm("You confirm to apply changes?")) applyChanges();
});

let ipv4s = document.querySelectorAll('.network[ipv4]');

document.addEventListener("DOMContentLoaded", () => requestData());

dhcp.addEventListener("change", function() {
    for (let input of ipv4s) {
        if (this.checked) input.disabled = true;
        else input.disabled = false;
    };
});

// Send data to save the configuration
function sendData(classData, objLength) {
    let bodyData = {};
    let flag = false;

    // validate form inputs before send
    if (classData == "network" && dhcp.checked) bodyData = validateData(document.querySelectorAll("#ssid, #wifiPass"));
    else bodyData = validateData(document.querySelectorAll(`.${classData}`));

    if (classData != "mqtt") {
        if (dhcp.checked) {
            bodyData["dhcp"] = 1;
            flag = true;
        }
        else {
            bodyData["dhcp"] = 0;
        }
    }

    if (Object.keys(bodyData).length == objLength || flag) {
        fetch(`http://${__IPADDRESS__}/${classData}`, {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify(bodyData)
        })
        .then(response => {
            if (response.headers.get("Content-Length") > 0) return response.json();
            return null;
        })
        .then(json => {
            if (json) alert(`${json.message}\n.`);
        });
    };
}

// Request data when the page is loaded
function requestData() {
    fetch(`http://${__IPADDRESS__}/data`, {
        method: "GET",
        headers: { "Content-Type": "application/json" }
    })
    .then(response => {
        if (response.headers.get("Content-Length") > 0) return response.json();
        return null;
    })
    .then(data => {
        if (data) loadContent(data);
    });
}

// Validate data before send to device
function validateData(elements) {
    let data = {};

    for (let element of elements) {
        if (element.type === "checkbox") continue;

        let parent = element.parentElement;
        let err = parent.querySelector("#error-message");

        if (element.value != "") {
            // if exists error message in parent, that remove.
            if (err) parent.removeChild(err);

            // apply IPV4 mask validation
            if (element.hasAttribute("ipv4")) {
                if (!isValidIP(element.value)) {
                    let err = showErrorMessage(`${element.id} is a invalid IPv4 address.`);
                    parent.appendChild(err);
                    continue;
                }
            }
            data[`${element.id}`] = element.value;
            continue;
        }

        // Apply error message
        if (element.value == "" && !err) {
            let err = showErrorMessage(`${element.id} must be filled`);
            parent.appendChild(err);
        }
    }

    return data;
}

function applyChanges() {
    alert("Device will restart in 5 seconds.");
    fetch(`http://${__IPADDRESS__}/restart`, { method: "GET" });
}

// Load the data received from device (if there's somenthing)
function loadContent(configs) {
    const inputsNetwork = document.querySelectorAll(".network");
    const inputsMqtt = document.querySelectorAll(".mqtt");

    if (configs.hasOwnProperty("status") && configs.status != null) {
        let parent = document.querySelector(".container-items");

        let index = 1;
        for (let key in configs.status) {
            if (key == "temperatures") {
                let tempIndex = 1;
                configs.status.temperatures.forEach(temp => {
                    let div = document.createElement("div");
                    div.id = `item${index}`;
                    div.classList.add("item", "panel-grid-column");

                    let h51 = document.createElement("h5");
                    h51.innerHTML = `Temperatura ${tempIndex}`;
                    div.appendChild(h51);

                    let h52 = document.createElement("h5");
                    h52.innerHTML = temp;
                    div.appendChild(h52);

                    parent.appendChild(div);

                    tempIndex++;
                    index++;
                });
                continue;
            }
            
            if (key == "wifiQuality") {
                let quality = configs.status["wifiQuality"];
                
                console.log(quality);

                if (quality != 0) {
                    let parentDiv = document.createElement("div");
                    parentDiv.classList.add("item", "panel-grid-column");
                    let h5 = document.createElement("h5");
                    h5.innerText = "Wi-fi quality";
                    let childDiv = document.createElement("div");
                    let divSquare = document.createElement("div"); 
                    
                    divSquare.style.width = "16px";
                    divSquare.style.height = "16px";
    
                    if (quality >= -50) divSquare.style.backgroundColor = "green";
                    else if (quality >= -70) divSquare.style.backgroundColor = "yellow";
                    else if (quality >= -90) divSquare.style.backgroundColor = "red";
    
                    childDiv.append(divSquare);
                    parentDiv.append(h5, childDiv);
                    parent.appendChild(parentDiv);
                }
                continue;
            }

            let div = document.createElement("div");
            div.id = `item${index}`;
            div.classList.add("item", "panel-grid-column");

            let h51 = document.createElement("h5");
            h51.innerHTML = key;
            div.appendChild(h51);

            let h52 = document.createElement("h5");
            h52.innerHTML = configs.status[`${key}`];
            div.appendChild(h52);

            parent.appendChild(div);
            index++;
        }
    }

    if (configs.hasOwnProperty("network") && configs.network != null) {
        inputsNetwork.forEach(input => {
            if (input.type == "checkbox" && configs.network[input.id] == 1) {
                input.checked = true;

                for (let inp of ipv4s) {
                    inp.disabled = true;
                };
            }
            input.value = configs.network[input.id];
        });
    }
    
    if (configs.hasOwnProperty("mqtt") && configs.mqtt != null) {
        inputsMqtt.forEach(input => {
            input.value = configs.mqtt[input.id];
        });
    }
}

function showErrorMessage(message) {
    let err = document.createElement("p");
    err.style.color = "red";
    err.id = "error-message";
    err.innerText = message;

    return err;
}

function isValidIP(str) {
    const octet = '(25[0-5]|2[0-4][0-9]|1[0-9][0-9]|[1-9][0-9]?|0)';
    const regex = new RegExp(`^${octet}\\.${octet}\\.${octet}\\.${octet}$`);
    return regex.test(str);
}