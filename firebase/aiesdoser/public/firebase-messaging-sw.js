importScripts("https://www.gstatic.com/firebasejs/10.12.2/firebase-app-compat.js");
importScripts("https://www.gstatic.com/firebasejs/10.12.2/firebase-messaging-compat.js");

firebase.initializeApp({
  apiKey: "AIzaSyB4XtC5Pvxw6To58EKTLMADQLqR_hTZK0M",
  authDomain: "aiesdoser.firebaseapp.com",
  databaseURL: "https://aiesdoser-default-rtdb.firebaseio.com",
  projectId: "aiesdoser",
  storageBucket: "aiesdoser.appspot.com",
  messagingSenderId: "449222824743",
  appId: "1:449222824743:web:06e34fc591d5af0f1a8f02"
});

const messaging = firebase.messaging();

messaging.onBackgroundMessage((payload) => {
  console.log("Background notification:", payload);

  self.registration.showNotification(
    payload.notification.title,
    {
      body: payload.notification.body,
      icon: "/icon.png"
    }
  );
});