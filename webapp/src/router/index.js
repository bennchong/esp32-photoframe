import { createRouter, createWebHistory } from "vue-router";
import MainApp from "../views/MainApp.vue";
import ProvisionPage from "../views/ProvisionPage.vue";

const routes = [
  {
    path: "/",
    name: "main",
    component: MainApp,
  },
  {
    path: "/provision",
    name: "provision",
    component: ProvisionPage,
  },
];

const router = createRouter({
  history: createWebHistory(),
  routes,
});

export default router;
